import spidev
import gpiod
import time
import sys

# --- Hardware Config ---
CHIP = 'gpiochip3'
HANDSHAKE_LINE = 21
SPI_BUS = 3
SPI_DEV = 0
SPI_SPEED_HZ = 5000

PAYLOAD_BYTES = 128
ITERATIONS = 5
DETECT_TIMEOUT_S = 5.0
DISPLAY_TIMEOUT_S = 15.0

# --- Init ---
chip = gpiod.Chip(CHIP)
handshake = chip.get_line(HANDSHAKE_LINE)
handshake.request(consumer='kyphone-test', type=gpiod.LINE_REQ_DIR_IN)

spi = spidev.SpiDev()
try:
    spi.open(SPI_BUS, SPI_DEV)
except FileNotFoundError:
    print(f"Error: /dev/spidev{SPI_BUS}.{SPI_DEV} not found.")
    sys.exit(1)

spi.max_speed_hz = SPI_SPEED_HZ
spi.mode = 0


def build_payload(text):
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:30]]
    payload += [0x00] * (PAYLOAD_BYTES - len(payload))
    return payload


def wait_for_handshake(value, timeout_s):
    t0 = time.monotonic()
    while int(handshake.get_value()) != value:
        if time.monotonic() - t0 > timeout_s:
            return None
        time.sleep(0.01)
    return time.monotonic() - t0


def run_tests():
    print(f"\n--- KyPhone SPI Timing Test ({ITERATIONS} iterations) ---")
    print(f"Pass criteria: detect <{DETECT_TIMEOUT_S}s | display <{DISPLAY_TIMEOUT_S}s | 5/5 succeed\n")

    failures = []

    for i in range(ITERATIONS):
        message = f"test msg {i+1}"
        print(f"[{i+1}/{ITERATIONS}] Waiting for READY...", end='\r')

        if wait_for_handshake(1, DISPLAY_TIMEOUT_S) is None:
            failures.append(f"[{i+1}] FAIL: Inkplate never became ready")
            continue

        print(f"[{i+1}/{ITERATIONS}] Sending: '{message}'")
        spi.xfer2(build_payload(message))

        detect_s = wait_for_handshake(0, DETECT_TIMEOUT_S)
        if detect_s is None:
            failures.append(f"[{i+1}] FAIL: transfer not detected within {DETECT_TIMEOUT_S}s")
            continue

        display_s = wait_for_handshake(1, DISPLAY_TIMEOUT_S)
        if display_s is None:
            failures.append(f"[{i+1}] FAIL: display not complete within {DISPLAY_TIMEOUT_S}s")
            continue

        total_s = detect_s + display_s
        status = "PASS" if detect_s < DETECT_TIMEOUT_S else "FAIL"
        print(f"[{i+1}/{ITERATIONS}] {status} | detect={detect_s:.2f}s | display={display_s:.2f}s | total={total_s:.2f}s")

        if detect_s >= DETECT_TIMEOUT_S:
            failures.append(f"[{i+1}] FAIL: detect={detect_s:.2f}s >= {DETECT_TIMEOUT_S}s")

    print(f"\n--- Results: {ITERATIONS - len(failures)}/{ITERATIONS} passed ---")
    for f in failures:
        print(f"  {f}")

    if failures:
        print("FAIL")
        sys.exit(1)
    else:
        print("PASS: All assertions met.")


try:
    run_tests()
finally:
    spi.close()
    handshake.release()
