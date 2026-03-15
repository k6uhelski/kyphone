import RPi.GPIO as GPIO
import time
import sys

# Radxa Rock 3A Pins
MOSI = 19
SCLK = 23
CS0  = 24

GPIO.setmode(GPIO.BOARD)
GPIO.setup([MOSI, SCLK, CS0], GPIO.OUT)

print("--- Radxa Wiring Verifier ---")
print(f"Toggling Pins: MOSI={MOSI}, SCLK={SCLK}, CS0={CS0}")
print("Watch the Inkplate Serial Monitor (Signal_Detector.ino)")

try:
    state = True
    while True:
        val = GPIO.HIGH if state else GPIO.LOW
        print(f"Setting pins to {'HIGH' if state else 'LOW'}...")
        GPIO.output(MOSI, val)
        GPIO.output(SCLK, val)
        GPIO.output(CS0, val)
        state = not state
        time.sleep(1)
except KeyboardInterrupt:
    print("\nExiting")
finally:
    GPIO.cleanup()
