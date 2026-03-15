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

spi.max_speed_hz = 50000 
spi.mode = 0 

def send_message(text):
    # PADDING: 2 dummy bytes + Header (0x02) + 30 bytes MSG + Padding
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:30]]
    payload += [0x00] * (34 - len(payload))
    
    print(f"Sending message: '{text}'...")
    
    # Retry Loop: The payload itself acts as the handshake
    start_time = time.time()
    attempts = 0
    while True:
        attempts += 1
        # xfer2 sends the full 34-byte payload
        # If the Inkplate is ready, rx[0] will be 0x06
        rx = spi.xfer2(payload)
        
        if rx[0] == 0x06:
            print(f"SUCCESS! Inkplate ACK received on attempt {attempts}.")
            return True
            
        if time.time() - start_time > 15:
            print("TIMEOUT: Inkplate is not responding.")
            return False
            
        # Wait a bit before retrying (Inkplate might be refreshing)
        time.sleep(0.5)

try:
    print("--- Radxa SPI Controller (Sync Payload Mode) ---")
    print("Wire Check: MOSI=19, MISO=21 (Yellow), SCLK=23, CS=24")
    while True:
        msg = input("KyPhone> ")
        if msg.lower() in ["exit", "quit"]: break
        if not msg: continue
        send_message(msg)
except KeyboardInterrupt:
    print("\nExiting")
finally:
    spi.close()
