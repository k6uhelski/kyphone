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

from twilio.rest import Client

# --- Twilio Config ---
ACCOUNT_SID   = os.environ.get('TWILIO_SID')
AUTH_TOKEN    = os.environ.get('TWILIO_TOKEN')
TWILIO_NUMBER = os.environ.get('TWILIO_NUMBER')

if not all([ACCOUNT_SID, AUTH_TOKEN, TWILIO_NUMBER]):
    if SIM_MODE:
        print("Warning: Twilio env vars not set — SMS polling disabled in sim mode.")
    else:
        print("Error: set TWILIO_SID, TWILIO_TOKEN, and TWILIO_NUMBER env vars.")
        sys.exit(1)

# --- Contacts ---
CONTACTS = {
    # '+12125550001': 'Kyle',
}

# --- SPI / Handshake Config ---
CHIP = 'gpiochip3'
HANDSHAKE_LINE = 21
SPI_BUS = 3
SPI_DEV = 0
SPI_SPEED_HZ = 5000
PAYLOAD_BYTES = 128

SMS_DISPLAY_DURATION = 10   # seconds to show SMS before returning to home
CLOCK_UPDATE_INTERVAL = 60  # seconds between home screen refreshes
SMS_POLL_INTERVAL = 2       # seconds between Twilio polls

# --- Persistence ---
DATA_DIR = os.path.join(os.path.dirname(__file__), '..', 'data')
MESSAGES_FILE = os.path.join(DATA_DIR, 'messages.json')

# --- Init SPI + Twilio (hardware mode only) ---
if not SIM_MODE:
    chip = gpiod.Chip(CHIP)
    handshake = chip.get_line(HANDSHAKE_LINE)
    handshake.request(consumer='kyphone-app', type=gpiod.LINE_REQ_DIR_IN)

    spi = spidev.SpiDev()
    try:
        spi.open(SPI_BUS, SPI_DEV)
    except FileNotFoundError:
        print(f"Error: /dev/spidev{SPI_BUS}.{SPI_DEV} not found.")
        sys.exit(1)
    spi.max_speed_hz = SPI_SPEED_HZ
    spi.mode = 0

client = Client(ACCOUNT_SID, AUTH_TOKEN) if all([ACCOUNT_SID, AUTH_TOKEN, TWILIO_NUMBER]) else None

# --- Simulator (sim mode only) ---
simulator = None
if SIM_MODE:
    from simulator import Simulator
    simulator = Simulator(lambda keycode: handle_key(keycode))


# --- State ---
state = {
    'running': True,
    'messages': [],      # list of {'sender': str, 'name': str, 'body': str, 'read': bool}
    'last_sid': None,
    'last_sender': None,
    'lock': threading.Lock(),
    'nav': {
        'screen': 'HOME',    # HOME | MSG_LIST | MSG_THREAD
        'selected': 0,       # selected index in MSG_LIST
        'thread_sender': None,
        'home_sel': -1,      # selected home button: -1=none 0=TEXT 1=CALL 2=READ 3=LISTEN
    },
}


# --- Persistence Helpers ---

def load_messages():
    try:
        with open(MESSAGES_FILE, 'r') as f:
            data = json.load(f)
        state['messages'] = data.get('messages', [])
        state['last_sid'] = data.get('last_sid')
        print(f"Loaded {len(state['messages'])} messages from disk.")
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


# --- SPI Helpers ---

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
    print(f"  → screen: {command[:60]}")
    if SIM_MODE:
        simulator.render(command)
        return
    if not wait_for_ready():
        print(f"Warning: Inkplate not ready, skipping: {command[:40]}")
        return
    spi.xfer2(build_payload(command))


# --- Screen Builders ---

def format_name(number):
    return CONTACTS.get(number, number)


def get_conversations():
    """Return list of most-recent messages per sender, newest first (max 4)."""
    with state['lock']:
        seen = {}
        for m in reversed(state['messages']):
            if m['sender'] not in seen:
                seen[m['sender']] = m
    return list(seen.values())[:4]


def push_home():
    now = datetime.now()
    time_str = now.strftime("%-I:%M %p")
    date_str = now.strftime("%a, %b %-d")
    with state['lock']:
        unread = sum(1 for m in state['messages'] if not m['read'])
        home_sel = state['nav']['home_sel']
    push_screen(f"HOME|{time_str}|{date_str}|{unread}|{home_sel}")


def push_msg_list(selected=0):
    conversations = get_conversations()
    if not conversations:
        push_screen("MSG_LIST|0|No messages yet")
        return
    # Clamp selected to valid range
    selected = max(0, min(selected, len(conversations) - 1))
    parts = [str(selected)]
    for m in conversations:
        name = m['name'][:10]
        preview = m['body'][:18]
        parts.append(f"{name}\xb7{preview}")  # middle dot separator
    push_screen("MSG_LIST|" + "|".join(parts))


def push_msg_thread(sender):
    """Push MSG_THREAD screen showing conversation with sender."""
    with state['lock']:
        thread = [m for m in state['messages'] if m['sender'] == sender]
    name = format_name(sender)
    parts = [name]
    for m in thread[-5:]:  # last 5 messages
        prefix = "You" if m['sender'] == TWILIO_NUMBER else ">"
        parts.append(f"{prefix}: {m['body'][:22]}")
    push_screen("MSG_THREAD|" + "|".join(parts))


def push_sms(sender_name, body):
    push_screen(f"{sender_name}|{body}")


# --- Navigation ---

def navigate_to(screen, selected=0, thread_sender=None):
    with state['lock']:
        state['nav']['screen'] = screen
        state['nav']['selected'] = selected
        state['nav']['thread_sender'] = thread_sender
        state['nav']['home_sel'] = -1

    if screen == 'HOME':
        push_home()
    elif screen == 'MSG_LIST':
        push_msg_list(selected)
    elif screen == 'MSG_THREAD':
        push_msg_thread(thread_sender)
        # Mark messages from this sender as read
        with state['lock']:
            for m in state['messages']:
                if m['sender'] == thread_sender:
                    m['read'] = True
        save_messages()


def handle_key(keycode):
    with state['lock']:
        screen = state['nav']['screen']
        selected = state['nav']['selected']
        thread_sender = state['nav']['thread_sender']

    conversations = get_conversations()

    if screen == 'HOME':
        if keycode == 'KEY_UP':
            with state['lock']:
                state['nav']['home_sel'] = -1
            push_home()
        elif keycode == 'KEY_LEFT':
            with state['lock']:
                cur = state['nav']['home_sel']
                state['nav']['home_sel'] = (cur - 1) % 4 if cur >= 0 else 3
            push_home()
        elif keycode in ('KEY_RIGHT', 'KEY_DOWN'):
            with state['lock']:
                cur = state['nav']['home_sel']
                state['nav']['home_sel'] = (cur + 1) % 4 if cur >= 0 else 0
            push_home()
        elif keycode == 'KEY_ENTER':
            with state['lock']:
                home_sel = state['nav']['home_sel']
            if home_sel == 0:  # TEXT
                navigate_to('MSG_LIST', selected=0)

    elif screen == 'MSG_LIST':
        if keycode in ('KEY_DOWN', 'KEY_RIGHT'):
            new_sel = min(selected + 1, max(0, len(conversations) - 1))
            navigate_to('MSG_LIST', selected=new_sel)
        elif keycode in ('KEY_UP', 'KEY_LEFT'):
            new_sel = max(selected - 1, 0)
            navigate_to('MSG_LIST', selected=new_sel)
        elif keycode == 'KEY_ENTER':
            if conversations and selected < len(conversations):
                sender = conversations[selected]['sender']
                navigate_to('MSG_THREAD', selected=selected, thread_sender=sender)
        elif keycode in ('KEY_BACKSPACE', 'KEY_ESC'):
            navigate_to('HOME')

    elif screen == 'MSG_THREAD':
        if keycode in ('KEY_BACKSPACE', 'KEY_ESC'):
            navigate_to('MSG_LIST', selected=selected)


def _restore_screen():
    """Return to the current nav screen after an SMS interruption."""
    with state['lock']:
        screen = state['nav']['screen']
        selected = state['nav']['selected']
        thread_sender = state['nav']['thread_sender']
    if screen == 'HOME':
        push_home()
    elif screen == 'MSG_LIST':
        push_msg_list(selected)
    elif screen == 'MSG_THREAD':
        push_msg_thread(thread_sender)


# --- Loops ---

def push_clock_tick():
    """Push full home layout but request partial update for faster refresh."""
    now = datetime.now()
    time_str = now.strftime("%-I:%M %p")
    date_str = now.strftime("%a, %b %-d")
    with state['lock']:
        unread = sum(1 for m in state['messages'] if not m['read'])
        home_sel = state['nav']['home_sel']
    push_screen(f"HOME_FAST|{time_str}|{date_str}|{unread}|{home_sel}")


def clock_loop():
    """Push full home screen on start, then partial clock tick every minute."""
    if SIM_MODE:
        while simulator is None or not simulator._ready:
            time.sleep(0.05)
    push_home()  # full refresh to set layout
    while state['running']:
        time.sleep(CLOCK_UPDATE_INTERVAL)
        if state['running']:
            with state['lock']:
                screen = state['nav']['screen']
            if screen == 'HOME':
                push_clock_tick()


def sms_loop():
    """Poll Twilio for new inbound SMS."""
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
                # New inbound message
                with state['lock']:
                    state['last_sid'] = messages[0].sid
                    state['last_sender'] = msg.from_
                    name = format_name(msg.from_)
                    state['messages'].append({
                        'sender': msg.from_,
                        'name': name,
                        'body': msg.body,
                        'read': False,
                    })
                    save_messages()
                print(f"\n[NEW SMS] {name}: {msg.body}")
                with state['lock']:
                    current_screen = state['nav']['screen']
                    thread_sender = state['nav']['thread_sender']
                if current_screen == 'MSG_THREAD' and thread_sender == msg.from_:
                    push_msg_thread(msg.from_)
                else:
                    _restore_screen()  # silently refresh current screen with updated badge
                break
        except Exception as e:
            print(f"Poll error: {e}")
        time.sleep(SMS_POLL_INTERVAL)


def send_reply(to_number, body):
    try:
        msg = client.messages.create(body=body, from_=TWILIO_NUMBER, to=to_number)
        with state['lock']:
            state['messages'].append({
                'sender': TWILIO_NUMBER,
                'name': 'You',
                'body': body,
                'read': True,
            })
        print(f"  → sent: {body} (SID: {msg.sid})")
    except Exception as e:
        print(f"  → send failed: {e}")


def main():
    # Load persisted messages from disk
    load_messages()

    # Seed last_sid to avoid replaying old messages (only if not loaded from disk)
    if state['last_sid'] is None:
        try:
            recent = client.messages.list(to=TWILIO_NUMBER, limit=1)
            if recent:
                state['last_sid'] = recent[0].sid
                print(f"Starting from SID: {state['last_sid']}")
        except Exception as e:
            print(f"Warning: could not seed SID: {e}")

    # Start background threads
    threading.Thread(target=clock_loop, daemon=True).start()
    threading.Thread(target=sms_loop, daemon=True).start()

    # Start keyboard navigation
    if not SIM_MODE:
        KeyboardHandler(handle_key).start()

    print("\n--- KyPhone OS ---")
    if TWILIO_NUMBER:
        print(f"Number: {TWILIO_NUMBER}")
    if SIM_MODE:
        print("Running in simulator mode. Use arrow keys + Enter to navigate.")

    try:
        if SIM_MODE:
            simulator.init()
            simulator.run_loop()  # blocks — pygame event loop on main thread
        elif sys.stdin.isatty():
            print("Type replies below. 'msgs' to see message list. 'home' for home screen. 'exit' to quit.\n")
            while True:
                reply = input("KyPhone> ").strip()
                if reply.lower() in ('exit', 'quit'):
                    break
                elif reply.lower() == 'home':
                    push_home()
                elif reply.lower() == 'msgs':
                    push_msg_list()
                elif not reply:
                    continue
                else:
                    with state['lock']:
                        last_sender = state['last_sender']
                    if last_sender is None:
                        print("No messages received yet — no sender to reply to.")
                    else:
                        send_reply(last_sender, reply)
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
