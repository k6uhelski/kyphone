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

spi.max_speed_hz = 500000 
spi.mode = 0

print("--- Radxa Loopback Test ---")
print("Make sure MOSI (Pin 19) is wired directly to MISO (Pin 21)!")
print("Sending: [0xDE, 0xAD, 0xBE, 0xEF]")

# Send a distinct byte pattern
tx_data = [0xDE, 0xAD, 0xBE, 0xEF]
rx_data = spi.xfer2(tx_data)

print(f"Received: [0x{rx_data[0]:02X}, 0x{rx_data[1]:02X}, 0x{rx_data[2]:02X}, 0x{rx_data[3]:02X}]")

if tx_data == rx_data:
    print("\nSUCCESS! Loopback test passed. Radxa Pins 19 and 21 are working.")
else:
    print("\nFAILURE! Data mismatch. Ensure Pin 19 is connected to Pin 21.")
    print("If they are connected, you may be using the wrong SPI bus or pins.")

spi.close()
