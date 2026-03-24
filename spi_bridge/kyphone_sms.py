import os
import sys
import time
import threading
import spidev
import gpiod
from twilio.rest import Client

# --- Twilio Config (set as env vars) ---
ACCOUNT_SID  = os.environ.get('TWILIO_SID')
AUTH_TOKEN   = os.environ.get('TWILIO_TOKEN')
TWILIO_NUMBER = os.environ.get('TWILIO_NUMBER')  # e.g. '+12125551234'

if not all([ACCOUNT_SID, AUTH_TOKEN, TWILIO_NUMBER]):
    print("Error: set TWILIO_SID, TWILIO_TOKEN, and TWILIO_NUMBER env vars.")
    sys.exit(1)

# --- Optional contacts (phone number → name) ---
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
POLL_INTERVAL_S = 2

# --- Init SPI ---
chip = gpiod.Chip(CHIP)
handshake = chip.get_line(HANDSHAKE_LINE)
handshake.request(consumer='kyphone-sms', type=gpiod.LINE_REQ_DIR_IN)

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


def format_sender(number):
    return CONTACTS.get(number, number)


def build_payload(text):
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:PAYLOAD_BYTES-3]]
    payload += [0x00] * (PAYLOAD_BYTES - len(payload))
    return payload


def wait_for_ready(timeout_s=10):
    t0 = time.monotonic()
    while int(handshake.get_value()) == 0:
        if time.monotonic() - t0 > timeout_s:
            return False
        time.sleep(0.01)
    return True


def push_to_display(sender, body):
    text = f"{sender}|{body}"
    if not wait_for_ready():
        print("Warning: Inkplate not ready, skipping display update.")
        return
    spi.xfer2(build_payload(text))
    print(f"  → pushed to display: [{sender}] {body}")


def send_reply(to_number, body):
    try:
        msg = client.messages.create(body=body, from_=TWILIO_NUMBER, to=to_number)
        print(f"  → sent to {to_number}: {body} (SID: {msg.sid})")
    except Exception as e:
        print(f"  → send failed: {e}")


def poll_loop(state):
    print(f"Polling for SMS every {POLL_INTERVAL_S}s...")
    while state['running']:
        try:
            messages = client.messages.list(to=TWILIO_NUMBER, limit=5)
            for msg in messages:
                if msg.sid == state['last_sid']:
                    break
                if msg.direction != 'inbound':
                    continue
                # New inbound message
                state['last_sid'] = messages[0].sid
                sender = format_sender(msg.from_)
                state['last_sender'] = msg.from_
                print(f"\n[NEW SMS] {sender}: {msg.body}")
                push_to_display(sender, msg.body)
                break
        except Exception as e:
            print(f"Poll error: {e}")
        time.sleep(POLL_INTERVAL_S)


def main():
    # Seed last_sid with the most recent message to avoid replaying old ones
    state = {'running': True, 'last_sid': None, 'last_sender': None}
    try:
        recent = client.messages.list(to=TWILIO_NUMBER, limit=1)
        if recent:
            state['last_sid'] = recent[0].sid
            print(f"Starting from message SID: {state['last_sid']}")
    except Exception as e:
        print(f"Warning: could not fetch recent messages: {e}")

    poller = threading.Thread(target=poll_loop, args=(state,), daemon=True)
    poller.start()

    print("\n--- KyPhone SMS ---")
    print(f"Your number: {TWILIO_NUMBER}")
    print("Friends can text this number. Type replies below.")
    print("Type 'exit' to quit.\n")

    try:
        while True:
            reply = input("KyPhone> ").strip()
            if reply.lower() in ('exit', 'quit'):
                break
            if not reply:
                continue
            if state['last_sender'] is None:
                print("No messages received yet — no sender to reply to.")
                continue
            send_reply(state['last_sender'], reply)
    except KeyboardInterrupt:
        pass
    finally:
        state['running'] = False
        spi.close()
        handshake.release()
        print("\nExiting.")


if __name__ == '__main__':
    main()
