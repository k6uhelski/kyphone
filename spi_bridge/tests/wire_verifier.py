import gpiod
import time
import sys

# Radxa Rock 3A SPI Pins on gpiochip3
# Pin 19 (MOSI) = GPIO3_B1 (Line 9)
# Pin 23 (SCLK) = GPIO3_B0 (Line 8)
# Pin 24 (CS0)  = GPIO3_B2 (Line 10)

CHIP = 'gpiochip3'
PIN_MOSI = 9
PIN_SCLK = 8
PIN_CS   = 10

chip = gpiod.Chip(CHIP)

# Request all three pins as output
lines = chip.get_lines([PIN_MOSI, PIN_SCLK, PIN_CS])
lines.request(consumer='kyphone-verifier', type=gpiod.LINE_REQ_DIR_OUT)

print("--- Radxa Wiring Verifier (gpiod) ---")
print(f"Toggling Pins on {CHIP}:")
print(f"  Pin 19 (MOSI) -> Line {PIN_MOSI}")
print(f"  Pin 23 (SCLK) -> Line {PIN_SCLK}")
print(f"  Pin 24 (CS0)  -> Line {PIN_CS}")
print("Watch the Inkplate Serial Monitor (Signal_Detector.ino)")

try:
    state = 1
    while True:
        print(f"Setting pins to {'HIGH' if state == 1 else 'LOW'}...")
        lines.set_values([state, state, state])
        state = 0 if state == 1 else 1
        time.sleep(1)
except KeyboardInterrupt:
    print("\nExiting")
finally:
    lines.release()
    chip.close()
