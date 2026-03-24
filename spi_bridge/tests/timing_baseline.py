import spidev
import gpiod
import time
import sys

# --- Hardware Config ---
CHIP = 'gpiochip3'
HANDSHAKE_LINE = 21  # Radxa Pin 13, yellow wire
SPI_BUS = 3
SPI_DEV = 0
SPI_SPEED_HZ = 5000 # 5kHz: known to deliver all 272 bits on Rockchip

PAYLOAD_BYTES = 128
MESSAGE = "timing baseline"
ITERATIONS = 3
DETECT_TIMEOUT_S = 120
DISPLAY_TIMEOUT_S = 120

# --- Init ---
chip = gpiod.Chip(CHIP)
handshake = chip.get_line(HANDSHAKE_LINE)
handshake.request(consumer='kyphone-baseline', type=gpiod.LINE_REQ_DIR_IN)

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


def wait_for_handshake(value, timeout_s, label):
    t0 = time.monotonic()
    while int(handshake.get_value()) != value:
        if time.monotonic() - t0 > timeout_s:
            return None  # timed out
        time.sleep(0.01)
    return time.monotonic() - t0


def run_baseline():
    print(f"\n--- KyPhone SPI Timing Baseline ({ITERATIONS} iterations) ---")
    print(f"Speed: {SPI_SPEED_HZ // 1000}kHz | Message: '{MESSAGE}'")
    print(f"Keep the ESP32 Serial Monitor open at 115200 baud.\n")

    results = []

    for i in range(ITERATIONS):
        print(f"[{i+1}/{ITERATIONS}] Waiting for Inkplate READY...", end='\r')
        elapsed = wait_for_handshake(1, DISPLAY_TIMEOUT_S, "READY")
        if elapsed is None:
            print(f"[{i+1}/{ITERATIONS}] SKIP: handshake never went HIGH (Inkplate not ready)")
            continue
        print(f"[{i+1}/{ITERATIONS}] Inkplate READY. Sending...          ")

        payload = build_payload(MESSAGE)

        t_send_start = time.monotonic()
        spi.xfer2(payload)
        t_send_end = time.monotonic()
        send_ms = (t_send_end - t_send_start) * 1000

        # Wait for handshake to go LOW (ESP32 detected transfer and started processing)
        detect_s = wait_for_handshake(0, DETECT_TIMEOUT_S, "DETECT")
        if detect_s is None:
            print(f"[{i+1}/{ITERATIONS}] TIMEOUT: handshake never went LOW. ESP32 did not detect transfer.")
            results.append({'send_ms': send_ms, 'detect_s': None, 'display_s': None})
            continue

        # Wait for handshake to go HIGH again (display refresh complete)
        display_s = wait_for_handshake(1, DISPLAY_TIMEOUT_S, "DISPLAY")
        if display_s is None:
            print(f"[{i+1}/{ITERATIONS}] TIMEOUT: display never finished (handshake stuck LOW).")
            results.append({'send_ms': send_ms, 'detect_s': detect_s, 'display_s': None})
            continue

        total_s = detect_s + display_s
        results.append({'send_ms': send_ms, 'detect_s': detect_s, 'display_s': display_s})
        print(f"[{i+1}/{ITERATIONS}] send={send_ms:.2f}ms | detect={detect_s:.1f}s | display={display_s:.1f}s | total={total_s:.1f}s")

    print("\n--- Summary ---")
    valid = [r for r in results if r['detect_s'] is not None]
    if not valid:
        print("No successful transfers to summarize.")
    else:
        avg_detect = sum(r['detect_s'] for r in valid) / len(valid)
        avg_display = sum(r['display_s'] for r in valid if r['display_s']) / len(valid)
        print(f"Avg detect latency : {avg_detect:.1f}s  (target: <5s)")
        print(f"Avg display latency: {avg_display:.1f}s")
        if avg_detect < 5.0:
            print("PASS: Transfer detection is within target.")
        else:
            print(f"SLOW: Detection takes {avg_detect:.1f}s. Primary bottleneck is the firmware timeout.")
            print(f"      Recommended timeout: {avg_detect * 0.1:.0f}ms  (based on max inter-chunk gap)")


try:
    run_baseline()
finally:
    spi.close()
    handshake.release()
