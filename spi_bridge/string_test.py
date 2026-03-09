import spidev
import time
import sys

# Initialize SPI on Bus 3, CS 0 (Controller Side)
spi = spidev.SpiDev()
try:
    spi.open(3, 0)
except FileNotFoundError:
    print("Error: /dev/spidev3.0 not found. Enable 'spi3' in rsetup.")
    sys.exit(1)

spi.max_speed_hz = 500000 
spi.mode = 0

def send_message(text):
    # Payload: [STX (0x02)] + [Up to 30 chars] + [Null padding]
    payload = [0x02] + [ord(c) for c in text[:30]]
    payload += [0x00] * (32 - len(payload))
    
    print(f"Sending SPI Payload: '{text}'")
    # Send twice to account for lack of handshake (increases sync chance)
    spi.xfer2(payload)
    time.sleep(0.05)
    spi.xfer2(payload)
    print("Done.")

try:
    print("--- Radxa SPI Controller (No-Handshake Mode) ---")
    print("Wire Check: MOSI=13, SCLK=14, CS=22. UNPLUG PIN 12.")
    while True:
        msg = input("KyPhone> ")
        if msg.lower() == "exit": break
        if not msg: continue
        send_message(msg)
except KeyboardInterrupt:
    print("\nExiting")
finally:
    spi.close()
