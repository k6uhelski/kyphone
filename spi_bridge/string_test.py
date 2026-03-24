import spidev
import time
import sys
import gpiod

# Radxa Pin 13 = gpiochip3, line 21
PIN_HANDSHAKE = 21 
CHIP_HANDSHAKE = 'gpiochip3'

# GPIO Setup for Handshake (using v1.x syntax)
chip = gpiod.Chip(CHIP_HANDSHAKE)
line = chip.get_line(PIN_HANDSHAKE)
line.request(consumer='kyphone', type=gpiod.LINE_REQ_DIR_IN)

# Initialize SPI on Bus 3, CS 0
spi = spidev.SpiDev()
try:
    spi.open(3, 0)
except FileNotFoundError:
    print("Error: /dev/spidev3.0 not found.")
    sys.exit(1)

spi.max_speed_hz = 5000 # 5kHz: slow enough for Rockchip DMA to deliver all 272 bits
spi.mode = 0 

def wait_for_ready():
    print("Waiting for Inkplate READY signal (Yellow Wire HIGH)...", end="\r")
    while int(line.get_value()) == 0:
        time.sleep(0.1)
    print("Inkplate is READY!                           ")

PAYLOAD_BYTES = 128

def send_message(text):
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:PAYLOAD_BYTES-3]]
    payload += [0x00] * (PAYLOAD_BYTES - len(payload))
    
    # HANDSHAKE: Wait for Inkplate to be ready
    wait_for_ready()
    
    print(f"Sending message: '{text}'...")
    
    spi.xfer2(payload)
    
    print(f"Message sent.")
    return True

try:
    print("--- Radxa SPI Controller (3-Wire + Expander Handshake) ---")
    print("Handshake: Pin 13 (Yellow Wire) MUST be HIGH to send.")
    while True:
        msg = input("KyPhone> ")
        if msg.lower() in ["exit", "quit"]: break
        if not msg: continue
        send_message(msg)
except KeyboardInterrupt:
    print("\nExiting")
finally:
    spi.close()
    line.release()
