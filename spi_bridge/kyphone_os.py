"""
kyphone_os.py — KyPhone OS 0.1

Screens: lock | home | texts_list | thread | compose | stub
Phase 2 (calls): calls_list | dial | outgoing | incoming | in_call

Run:
    python3 spi_bridge/kyphone_os.py          # hardware mode (Radxa)
    python3 spi_bridge/kyphone_os.py --sim    # simulator (Mac)
"""

import os
import sys
import time
import json
import threading
from datetime import datetime

SIM_MODE = '--sim' in sys.argv

if not SIM_MODE:
    import spidev
    import gpiod
    from input_handler import KeyboardHandler
    from trackpad_handler import TrackpadHandler

from twilio.rest import Client

# --- Config ---
CHIP            = 'gpiochip3'
HANDSHAKE_LINE  = 21
SPI_BUS         = 3
SPI_DEV         = 0
SPI_SPEED_HZ    = 10000
PAYLOAD_BYTES   = 256

SMS_POLL_INTERVAL    = 2
CLOCK_UPDATE_INTERVAL = 60

# --- Twilio Credentials ---
ACCOUNT_SID   = os.environ.get('TWILIO_SID')
AUTH_TOKEN    = os.environ.get('TWILIO_TOKEN')
TWILIO_NUMBER = os.environ.get('TWILIO_NUMBER') or ('+1sim' if SIM_MODE else None)

if not all([ACCOUNT_SID, AUTH_TOKEN, TWILIO_NUMBER]):
    if not SIM_MODE and os.environ.get('TWILIO_NUMBER'):
        print("Warning: Twilio env vars not set — SMS polling disabled.")
    elif not SIM_MODE:
        print("Error: set TWILIO_SID, TWILIO_TOKEN, and TWILIO_NUMBER env vars.")
        sys.exit(1)

# --- Contacts ---
_contacts_path = os.path.join(os.path.dirname(__file__), '..', 'data', 'contacts.json')
try:
    with open(_contacts_path) as _f:
        CONTACTS = json.load(_f)
except FileNotFoundError:
    CONTACTS = {}

# --- Persistence Paths ---
DATA_DIR      = os.path.join(os.path.dirname(__file__), '..', 'data')
MESSAGES_FILE = os.path.join(DATA_DIR, 'messages.json')

# --- Lock Screen Quotes ---
# Replace with your chosen list. Quotes cycle each time the device returns to lock.
QUOTES = [
    "Smile, breathe, and go slowly.",
    "The present moment is the only moment available to us, and it is the door to all moments.",
    "Because you are alive, everything is possible.",
    "The most precious gift we can offer others is our presence.",
    "Peace is present right here and now, in ourselves and in everything we do and see.",
    "Life can be found only in the present moment.",
    "Walk as if you are kissing the Earth with your feet.",
    "Our own life has to be our message.",
    "Drink your tea slowly and reverently.",
    "Sometimes your joy is the source of your smile, but sometimes your smile can be the source of your joy.",
    "Hope is important because it can make the present moment less difficult to bear.",
    "The mind can go in a thousand directions, but on this beautiful path, I walk in peace.",
    "There is no path to peace — peace is the path.",
    "Letting go gives us freedom, and freedom is the only condition for happiness.",
    "Waking up this morning, I smile. Twenty-four brand new hours are before me.",
    "Feelings come and go like clouds in a windy sky. Conscious breathing is my anchor.",
    "People have a hard time letting go of their suffering. Out of a fear of the unknown, they prefer suffering that is familiar.",
    "The seed of suffering in you may be strong, but don't wait until you have no more suffering before allowing yourself to be happy.",
    "We are more than our pain.",
    "If you love someone but rarely make yourself available to them, that is not true love.",
]

# --- State ---
state = {
    'screen':           'lock',
    'home_index':       0,          # 0=TEXT 1=CALL 2=READ 3=LISTEN
    'texts_index':      0,          # -1=header row selected
    'texts_header_sel': 'back',     # 'back' | 'plus'
    'thread_id':        None,       # sender phone number
    'thread_draft':     '',
    'thread_header_sel': None,      # None=typing | 'back' | 'info'
    'compose_to':       '',
    'compose_msg':      '',
    'compose_to_active': True,
    'compose_header_sel': None,     # None=typing | 'x'
    'stub_name':        '',
    'quote_index':      0,
    'messages':         [],         # [{sender, name, body, read}]
    'last_sid':         None,
    'running':          True,
    'lock':             threading.Lock(),
}

_spi_lock       = threading.Lock()  # serializes the SPI sender thread's own transfers
_pending_lock   = threading.Lock()
_pending_command = None
_pending_event  = threading.Event()

# --- Hardware Init ---
if not SIM_MODE:
    chip      = gpiod.Chip(CHIP)
    handshake = chip.get_line(HANDSHAKE_LINE)
    handshake.request(consumer='kyphone-os', type=gpiod.LINE_REQ_DIR_IN)

    spi = spidev.SpiDev()
    try:
        spi.open(SPI_BUS, SPI_DEV)
    except FileNotFoundError:
        print(f"Error: /dev/spidev{SPI_BUS}.{SPI_DEV} not found.")
        sys.exit(1)
    spi.max_speed_hz = SPI_SPEED_HZ
    spi.mode = 0

client = Client(ACCOUNT_SID, AUTH_TOKEN) if all([ACCOUNT_SID, AUTH_TOKEN]) else None

# --- Simulator ---
simulator = None
if SIM_MODE:
    from simulator import Simulator
    simulator = Simulator(lambda keycode: handle_key(keycode))


# ─── Helpers ──────────────────────────────────────────────────────────────────

def format_name(number):
    return CONTACTS.get(number, number)


def get_threads():
    """Group flat messages by sender. Return newest-first list (max 7 threads)."""
    with state['lock']:
        msgs = list(state['messages'])

    thread_map = {}
    for i, m in enumerate(msgs):
        s = m['sender']
        if s not in thread_map:
            thread_map[s] = {
                'sender': s,
                'name': format_name(s),
                'messages': [],
                'unread': False,
                '_last_i': i,
            }
        thread_map[s]['messages'].append(m)
        thread_map[s]['_last_i'] = i
        if not m['read']:
            thread_map[s]['unread'] = True

    sorted_threads = sorted(thread_map.values(), key=lambda t: t['_last_i'], reverse=True)
    for t in sorted_threads:
        del t['_last_i']
    return sorted_threads[:7]


# ─── SPI ──────────────────────────────────────────────────────────────────────

def wait_for_ready(timeout_s=10):
    if SIM_MODE:
        return True
    t0 = time.monotonic()
    while int(handshake.get_value()) == 0:
        if time.monotonic() - t0 > timeout_s:
            return False
        time.sleep(0.01)
    return True


def build_payload(text):
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:PAYLOAD_BYTES - 3]]
    payload += [0x00] * (PAYLOAD_BYTES - len(payload))
    return payload


def push_screen(command):
    """Queue a screen command. Non-blocking — the actual SPI transfer happens
    on a dedicated sender thread (see _spi_sender_loop), so a caller (e.g.
    handle_key, invoked directly from the keyboard/trackpad's read loop)
    never blocks on hardware I/O. If commands arrive faster than the SPI
    transfer + e-ink refresh can keep up (~1s each), only the latest one
    is kept — a fast burst of input coalesces to the final state instead
    of rendering every intermediate frame."""
    print(f"  → {command[:80]}")
    if SIM_MODE:
        simulator.render(command)
        return
    global _pending_command
    with _pending_lock:
        _pending_command = command
    _pending_event.set()


def _spi_sender_loop():
    global _pending_command
    while state['running']:
        _pending_event.wait()
        with _pending_lock:
            command = _pending_command
            _pending_command = None
            _pending_event.clear()
        if command is None:
            continue
        with _spi_lock:
            if not wait_for_ready():
                print(f"Warning: Inkplate not ready, skipping: {command[:40]}")
                continue
            spi.xfer2(build_payload(command))


# ─── Screen Builders ──────────────────────────────────────────────────────────

def push_lock():
    now = datetime.now()
    time_str = now.strftime("%-I:%M %p")
    date_str = now.strftime("%A, %B %-d").upper()
    quote = QUOTES[state['quote_index'] % len(QUOTES)]
    # Truncate quote to fit within PAYLOAD_BYTES (prefix + separators ≈ 30 chars overhead)
    max_quote = PAYLOAD_BYTES - 3 - len("LOCK|") - len(time_str) - len(date_str) - len("— THICH NHAT HANH") - 4
    push_screen(f"LOCK|{time_str}|{date_str}|{quote[:max_quote]}|— THICH NHAT HANH")


def push_home2():
    now = datetime.now()
    time_str = now.strftime("%-I:%M %p")
    with state['lock']:
        unread = sum(1 for m in state['messages'] if not m['read'])
        home_index = state['home_index']
    push_screen(f"HOME2|{time_str}|{home_index}|{unread}")


def push_texts():
    threads = get_threads()
    with state['lock']:
        idx = state['texts_index']
        hdr = state['texts_header_sel']

    # Clamp row index
    if idx >= 0 and threads:
        idx = min(idx, len(threads) - 1)

    # Encode header selection: -1=back button, -2=plus button
    if idx == -1:
        send_idx = -2 if hdr == 'plus' else -1
    else:
        send_idx = idx

    parts = [str(send_idx)]
    for t in threads:
        name    = t['name'][:10]
        preview = t['messages'][-1]['body'][:44] if t['messages'] else ''
        unread  = '1' if t['unread'] else '0'
        parts.append(f"{name}\xb7{preview}\xb7{unread}")
    push_screen("TEXTS|" + "|".join(parts))


def push_thread2():
    with state['lock']:
        thread_id  = state['thread_id']
        draft      = state['thread_draft']
        header_sel = state['thread_header_sel']
        thread_msgs = [m for m in state['messages'] if m['sender'] == thread_id]

    name = format_name(thread_id) if thread_id else ''
    hdr  = {'back': 'B', 'info': 'I'}.get(header_sel, '')
    parts = [name, draft[:40], hdr]
    for m in thread_msgs[-4:]:
        if not m['body'].strip():
            continue
        prefix = "Y" if m['sender'] == TWILIO_NUMBER else "R"
        parts.append(f"{prefix}:{m['body'][:28]}")
    push_screen("THREAD2|" + "|".join(parts))


def push_compose():
    with state['lock']:
        to        = state['compose_to'][:40]
        msg       = state['compose_msg'][:60]
        to_active = '1' if state['compose_to_active'] else '0'
        hdr       = 'X' if state['compose_header_sel'] == 'x' else ''
    push_screen(f"COMPOSE|{to}|{msg}|{to_active}|{hdr}")


def push_stub():
    with state['lock']:
        name = state['stub_name']
    push_screen(f"STUB|{name}")


# ─── State Machine ────────────────────────────────────────────────────────────

def handle_key(keycode):
    with state['lock']:
        screen = state['screen']
    print(f"[handle_key] screen={screen} keycode={keycode}")

    # 'Q' is a universal back/up shortcut (same as Esc), and WASD mirrors
    # the arrow keys, except where they're needed as real typed characters
    # in message/contact text entry.
    if screen not in ('thread', 'compose'):
        if keycode in ('CHAR:q', 'CHAR:Q'):
            keycode = 'KEY_ESC'
        elif keycode in ('CHAR:w', 'CHAR:W'):
            keycode = 'KEY_UP'
        elif keycode in ('CHAR:a', 'CHAR:A'):
            keycode = 'KEY_LEFT'
        elif keycode in ('CHAR:s', 'CHAR:S'):
            keycode = 'KEY_DOWN'
        elif keycode in ('CHAR:d', 'CHAR:D'):
            keycode = 'KEY_RIGHT'

    if screen == 'lock':
        _from_lock(keycode)

    elif screen == 'home':
        _from_home(keycode)

    elif screen == 'texts_list':
        _from_texts_list(keycode)

    elif screen == 'thread':
        _from_thread(keycode)

    elif screen == 'compose':
        _from_compose(keycode)

    elif screen == 'stub':
        if keycode == 'KEY_ESC':
            with state['lock']:
                state['screen'] = 'home'
            push_home2()


def _from_lock(keycode):
    with state['lock']:
        state['screen'] = 'home'
        state['home_index'] = 0
    push_home2()


def _from_home(keycode):
    if keycode in ('KEY_DOWN', 'KEY_RIGHT'):
        with state['lock']:
            old = state['home_index']
            state['home_index'] = min(3, old + 1)
            changed = state['home_index'] != old
        if changed:
            push_home2()
    elif keycode in ('KEY_UP', 'KEY_LEFT'):
        with state['lock']:
            old = state['home_index']
            state['home_index'] = max(-1, old - 1)  # -1 = KYPHONE header selected
            changed = state['home_index'] != old
        if changed:
            push_home2()
    elif keycode == 'KEY_ENTER':
        with state['lock']:
            idx = state['home_index']
        if idx == -1:  # KYPHONE header — same as Esc
            with state['lock']:
                state['screen'] = 'lock'
                state['quote_index'] += 1
            push_lock()
        elif idx == 0:   # TEXT
            with state['lock']:
                state['screen']           = 'texts_list'
                state['texts_index']      = 0
                state['texts_header_sel'] = 'back'
            push_texts()
        elif idx == 1:  # CALL — Phase 2
            pass
        elif idx in (2, 3):  # READ, LISTEN
            app_names = ['TEXT', 'CALL', 'READ', 'LISTEN']
            with state['lock']:
                state['screen']    = 'stub'
                state['stub_name'] = app_names[idx]
            push_stub()
    elif keycode == 'KEY_ESC':
        with state['lock']:
            state['screen'] = 'lock'
            state['quote_index'] += 1
        push_lock()


def _from_texts_list(keycode):
    with state['lock']:
        idx = state['texts_index']
        hdr = state['texts_header_sel']

    threads = get_threads()
    max_idx = max(0, len(threads) - 1)

    if keycode == 'KEY_DOWN':
        changed = False
        if idx == -1:
            # From header → first row
            with state['lock']:
                state['texts_index'] = 0
            changed = True
        else:
            new_idx = min(idx + 1, max_idx)
            if new_idx != idx:
                with state['lock']:
                    state['texts_index'] = new_idx
                changed = True
        if changed:
            push_texts()

    elif keycode == 'KEY_UP':
        changed = False
        if idx == 0:
            with state['lock']:
                state['texts_index']      = -1
                state['texts_header_sel'] = 'back'
            changed = True
        elif idx > 0:
            with state['lock']:
                state['texts_index'] = idx - 1
            changed = True
        if changed:
            push_texts()

    elif keycode == 'KEY_RIGHT' and idx == -1:
        with state['lock']:
            changed = state['texts_header_sel'] != 'plus'
            state['texts_header_sel'] = 'plus'
        if changed:
            push_texts()

    elif keycode == 'KEY_LEFT' and idx == -1:
        with state['lock']:
            changed = state['texts_header_sel'] != 'back'
            state['texts_header_sel'] = 'back'
        if changed:
            push_texts()

    elif keycode == 'KEY_ENTER':
        if idx == -1 and hdr == 'back':
            with state['lock']:
                state['screen'] = 'home'
            push_home2()
        elif idx == -1 and hdr == 'plus':
            _open_compose()
        elif idx >= 0 and threads and idx < len(threads):
            _open_thread(threads[idx]['sender'])

    elif keycode in ('CHAR:+',):
        _open_compose()

    elif keycode in ('KEY_ESC', 'KEY_BACKSPACE'):
        with state['lock']:
            state['screen'] = 'home'
        push_home2()


def _open_thread(sender):
    with state['lock']:
        state['screen']            = 'thread'
        state['thread_id']         = sender
        state['thread_draft']      = ''
        state['thread_header_sel'] = None
        for m in state['messages']:
            if m['sender'] == sender:
                m['read'] = True
    save_messages()
    push_thread2()


def _open_compose():
    with state['lock']:
        state['screen']            = 'compose'
        state['compose_to']        = ''
        state['compose_msg']       = ''
        state['compose_to_active'] = True
        state['compose_header_sel'] = None
    push_compose()


def _from_thread(keycode):
    with state['lock']:
        header_sel = state['thread_header_sel']

    if keycode == 'KEY_ESC':
        with state['lock']:
            state['screen'] = 'texts_list'
        push_texts()

    elif keycode == 'KEY_UP':
        if header_sel is None:
            with state['lock']:
                state['thread_header_sel'] = 'back'
            push_thread2()
        # already at the header — nothing further up

    elif header_sel is not None and keycode == 'KEY_DOWN':
        with state['lock']:
            state['thread_header_sel'] = None
        push_thread2()

    elif header_sel is not None and keycode == 'KEY_RIGHT':
        with state['lock']:
            changed = state['thread_header_sel'] != 'info'
            state['thread_header_sel'] = 'info'
        if changed:
            push_thread2()

    elif header_sel is not None and keycode == 'KEY_LEFT':
        with state['lock']:
            changed = state['thread_header_sel'] != 'back'
            state['thread_header_sel'] = 'back'
        if changed:
            push_thread2()

    elif header_sel is not None and keycode == 'KEY_ENTER':
        if header_sel == 'back':
            with state['lock']:
                state['screen'] = 'texts_list'
            push_texts()
        # 'info' has no page yet — reserved for later

    elif header_sel is None and keycode == 'KEY_BACKSPACE':
        with state['lock']:
            state['thread_draft'] = state['thread_draft'][:-1]
        push_thread2()

    elif header_sel is None and keycode == 'KEY_ENTER':
        with state['lock']:
            draft     = state['thread_draft'].strip()
            thread_id = state['thread_id']
        if draft:
            with state['lock']:
                state['thread_draft'] = ''
            send_reply(thread_id, draft)
            push_thread2()

    elif header_sel is None and keycode.startswith('CHAR:'):
        char = keycode[5:]
        with state['lock']:
            state['thread_draft'] += char
        push_thread2()


def _from_compose(keycode):
    with state['lock']:
        header_sel = state['compose_header_sel']

    if keycode == 'KEY_ESC':
        with state['lock']:
            state['screen'] = 'texts_list'
        push_texts()

    elif keycode == 'KEY_UP':
        if header_sel is None:
            with state['lock']:
                state['compose_header_sel'] = 'x'
            push_compose()
        # already at the header — nothing further up

    elif header_sel is not None and keycode == 'KEY_DOWN':
        with state['lock']:
            state['compose_header_sel'] = None
        push_compose()

    elif header_sel is not None and keycode == 'KEY_ENTER':
        # only target in the compose header is 'x' — exit
        with state['lock']:
            state['screen'] = 'texts_list'
        push_texts()

    elif header_sel is None and keycode == 'KEY_TAB':
        with state['lock']:
            state['compose_to_active'] = not state['compose_to_active']
        push_compose()

    elif header_sel is None and keycode == 'KEY_BACKSPACE':
        with state['lock']:
            if state['compose_to_active']:
                state['compose_to'] = state['compose_to'][:-1]
            else:
                state['compose_msg'] = state['compose_msg'][:-1]
        push_compose()

    elif header_sel is None and keycode == 'KEY_ENTER':
        with state['lock']:
            to_active = state['compose_to_active']
            to_val    = state['compose_to'].strip()
            msg_val   = state['compose_msg'].strip()
        if to_active:
            if to_val:
                with state['lock']:
                    state['compose_to_active'] = False
                push_compose()
        else:
            if to_val and msg_val:
                send_reply(to_val, msg_val)
                with state['lock']:
                    state['screen']    = 'thread'
                    state['thread_id'] = to_val
                    state['thread_draft'] = ''
                push_thread2()

    elif header_sel is None and keycode.startswith('CHAR:'):
        char = keycode[5:]
        with state['lock']:
            if state['compose_to_active']:
                state['compose_to'] += char
            else:
                state['compose_msg'] += char
        push_compose()


# ─── Persistence ──────────────────────────────────────────────────────────────

def load_messages():
    try:
        with open(MESSAGES_FILE, 'r') as f:
            data = json.load(f)
        state['messages'] = data.get('messages', [])
        state['last_sid']  = data.get('last_sid')
        print(f"Loaded {len(state['messages'])} messages.")
    except FileNotFoundError:
        pass
    except Exception as e:
        print(f"Warning: could not load messages: {e}")


def save_messages():
    try:
        os.makedirs(DATA_DIR, exist_ok=True)
        with open(MESSAGES_FILE, 'w') as f:
            json.dump({'messages': state['messages'], 'last_sid': state['last_sid']}, f)
    except Exception as e:
        print(f"Warning: could not save messages: {e}")


# ─── Twilio ───────────────────────────────────────────────────────────────────

def send_reply(to_number, body):
    if not SIM_MODE and client is not None:
        try:
            msg = client.messages.create(body=body, from_=TWILIO_NUMBER, to=to_number)
            print(f"  → sent: {body} (SID: {msg.sid})")
        except Exception as e:
            print(f"  → send failed: {e}")
            return
    with state['lock']:
        state['messages'].append({
            'sender': TWILIO_NUMBER,
            'name':   'You',
            'body':   body,
            'read':   True,
        })
    save_messages()


# ─── Background Loops ─────────────────────────────────────────────────────────

def clock_loop():
    if SIM_MODE:
        while simulator is None or not simulator._ready:
            time.sleep(0.05)
    push_lock()
    while state['running']:
        time.sleep(CLOCK_UPDATE_INTERVAL)
        if not state['running']:
            break
        with state['lock']:
            screen = state['screen']
        if screen == 'home':
            push_home2()
        elif screen == 'lock':
            push_lock()


def sms_loop():
    if client is None:
        print("SMS polling disabled (no Twilio credentials).")
        return
    print(f"Polling for SMS every {SMS_POLL_INTERVAL}s...")
    while state['running']:
        try:
            messages = client.messages.list(to=TWILIO_NUMBER, limit=5)
            for msg in messages:
                if msg.sid == state['last_sid']:
                    break
                if msg.direction != 'inbound':
                    continue
                with state['lock']:
                    state['last_sid'] = messages[0].sid
                    name = format_name(msg.from_)
                    state['messages'].append({
                        'sender': msg.from_,
                        'name':   name,
                        'body':   msg.body,
                        'read':   False,
                    })
                save_messages()
                print(f"\n[NEW SMS] {name}: {msg.body}")
                with state['lock']:
                    current_screen = state['screen']
                    thread_id      = state['thread_id']
                if current_screen == 'thread' and thread_id == msg.from_:
                    push_thread2()
                elif current_screen in ('texts_list',):
                    push_texts()
                # Other screens: message waits silently (badge visible on home/texts)
                break
        except Exception as e:
            print(f"Poll error: {e}")
        time.sleep(SMS_POLL_INTERVAL)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    load_messages()

    # Backfill recent messages from Twilio on first run
    if not SIM_MODE and state['last_sid'] is None and client is not None:
        try:
            recent  = client.messages.list(to=TWILIO_NUMBER, limit=20)
            if recent:
                state['last_sid'] = recent[0].sid
            inbound = [m for m in recent if m.direction == 'inbound']
            for msg in reversed(inbound):
                state['messages'].append({
                    'sender': msg.from_,
                    'name':   format_name(msg.from_),
                    'body':   msg.body,
                    'read':   True,
                })
            if inbound:
                save_messages()
                print(f"Backfilled {len(inbound)} messages.")
        except Exception as e:
            print(f"Warning: could not backfill: {e}")

    threading.Thread(target=clock_loop, daemon=True).start()
    threading.Thread(target=sms_loop,   daemon=True).start()

    if not SIM_MODE:
        threading.Thread(target=_spi_sender_loop, daemon=True).start()
        KeyboardHandler(handle_key).start()
        TrackpadHandler(handle_key).start()

    print("\n--- KyPhone OS 0.1 ---")
    if TWILIO_NUMBER and not SIM_MODE:
        print(f"Number: {TWILIO_NUMBER}")

    try:
        if SIM_MODE:
            simulator.init()
            simulator.run_loop()
        elif sys.stdin.isatty():
            while True:
                cmd = input("KyPhone> ").strip()
                if cmd.lower() in ('exit', 'quit'):
                    break
                elif cmd.lower() == 'home':
                    with state['lock']:
                        state['screen'] = 'home'
                    push_home2()
                elif cmd.lower() == 'texts':
                    with state['lock']:
                        state['screen']      = 'texts_list'
                        state['texts_index'] = 0
                    push_texts()
        else:
            while state['running']:
                time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        state['running'] = False
        if not SIM_MODE:
            spi.close()
            handshake.release()
        print("\nExiting.")


if __name__ == '__main__':
    main()
