import gpiod
import time
import sys

# Radxa Pin 13 = gpiochip3, line 21
CHIP = 'gpiochip3'
LINE = 21

print("--- Radxa Yellow Wire Listener (v1.x) ---")

try:
    chip = gpiod.Chip(CHIP)
    line = chip.get_line(LINE)
    line.request(consumer='kyphone', type=gpiod.LINE_REQ_DIR_IN)
    
    print(f"Listening on {CHIP} Line {LINE}... Ctrl+C to stop.")
    while True:
        val = line.get_value()
        print(f"Current Value: {val}    ", end="\r")
        time.sleep(0.1)
except Exception as e:
    print(f"\nError: {e}")
    print("If 'Device or resource busy', run: sudo gpiodetect")
finally:
    if 'line' in locals():
        line.release()
