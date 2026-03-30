import os
import sys
import time
import threading
from datetime import datetime
import spidev
import gpiod
from twilio.rest import Client

# --- Twilio Config ---
ACCOUNT_SID   = os.environ.get('TWILIO_SID')
AUTH_TOKEN    = os.environ.get('TWILIO_TOKEN')
TWILIO_NUMBER = os.environ.get('TWILIO_NUMBER')

if not all([ACCOUNT_SID, AUTH_TOKEN, TWILIO_NUMBER]):
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

# --- Init SPI ---
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

# --- Init Twilio ---
client = Client(ACCOUNT_SID, AUTH_TOKEN)


# --- State ---
state = {
    'running': True,
    'messages': [],      # list of {'sender': str, 'name': str, 'body': str, 'read': bool}
    'last_sid': None,
    'last_sender': None,
    'lock': threading.Lock(),
}


# --- SPI Helpers ---

def wait_for_ready(timeout_s=10):
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
    if not wait_for_ready():
        print(f"Warning: Inkplate not ready, skipping: {command[:40]}")
        return
    spi.xfer2(build_payload(command))
    print(f"  → screen: {command[:60]}")


# --- Screen Builders ---

def format_name(number):
    return CONTACTS.get(number, number)


def push_home():
    now = datetime.now()
    time_str = now.strftime("%-I:%M %p")
    date_str = now.strftime("%a, %b %-d")
    with state['lock']:
        unread = sum(1 for m in state['messages'] if not m['read'])
    if unread == 0:
        notif = "No new messages"
    elif unread == 1:
        notif = "1 new message"
    else:
        notif = f"{unread} new messages"
    push_screen(f"HOME|{time_str}|{date_str}|{notif}")


def push_msg_list():
    with state['lock']:
        # Deduplicate by sender, keep most recent
        seen = {}
        for m in reversed(state['messages']):
            if m['sender'] not in seen:
                seen[m['sender']] = m
        entries = list(seen.values())[:4]  # max 4 entries

    if not entries:
        push_screen("MSG_LIST|No messages yet")
        return

    parts = []
    for m in entries:
        name = m['name'][:10]
        preview = m['body'][:18]
        parts.append(f"{name}\xb7{preview}")  # middle dot separator

    push_screen("MSG_LIST|" + "|".join(parts))


def push_sms(sender_name, body):
    push_screen(f"{sender_name}|{body}")


# --- Loops ---

def clock_loop():
    """Push home screen every minute."""
    push_home()  # push immediately on start
    while state['running']:
        time.sleep(CLOCK_UPDATE_INTERVAL)
        if state['running']:
            push_home()


def sms_loop():
    """Poll Twilio for new inbound SMS."""
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
                print(f"\n[NEW SMS] {name}: {msg.body}")
                push_sms(name, msg.body)
                # Return to home after SMS_DISPLAY_DURATION seconds
                threading.Timer(SMS_DISPLAY_DURATION, push_home).start()
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
    # Seed last_sid to avoid replaying old messages
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

    print("\n--- KyPhone OS ---")
    print(f"Number: {TWILIO_NUMBER}")
    print("Type replies below. 'msgs' to see message list. 'home' for home screen. 'exit' to quit.\n")

    try:
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
    except KeyboardInterrupt:
        pass
    finally:
        state['running'] = False
        spi.close()
        handshake.release()
        print("\nExiting.")


if __name__ == '__main__':
    main()
