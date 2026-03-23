import spidev
import time
import sys

# Initialize SPI on Bus 3, CS 0
spi = spidev.SpiDev()
try:
    spi.open(3, 0)
except FileNotFoundError:
    print("Error: /dev/spidev3.0 not found.")
    sys.exit(1)

spi.max_speed_hz = 5000 
spi.mode = 0 

def send_message(text):
    # PADDING: 2 dummy bytes + Header (0x02) + 30 bytes MSG + Padding
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:30]]
    payload += [0x00] * (34 - len(payload))
    
    # NO HANDSHAKE - Just send immediately
    print(f"Sending message: '{text}'...")
    
    # xfer2 sends the full 34-byte payload
    spi.xfer2(payload)
    
    print(f"Message sent.")
    time.sleep(0.5) 
    return True

try:
    print("--- Radxa SPI Controller (NAKED TEST MODE) ---")
    print("Handshake is DISABLED. Sending blindly.")
    while True:
        msg = input("KyPhone> ")
        if msg.lower() in ["exit", "quit"]: break
        if not msg: continue
        send_message(msg)
except KeyboardInterrupt:
    print("\nExiting")
finally:
    spi.close()
