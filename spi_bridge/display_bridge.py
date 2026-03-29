import time
import struct
import spidev
import gpiod
from PIL import Image

# --- Config ---
CHIP = 'gpiochip3'
HANDSHAKE_LINE = 21
SPI_BUS = 3
SPI_DEV = 0
SPI_SPEED_HZ = 5000

FB_PATH = '/dev/fb0'
FB_WIDTH = 3840
FB_HEIGHT = 2160
DISPLAY_SIZE = 600        # Inkplate 4 TEMPERA is 600x600
FPS = 5
FRAME_INTERVAL = 1.0 / FPS

PAYLOAD_BYTES = 128
HEADER_BYTES = 11         # [0x00, 0x00, 0x03, x_hi, x_lo, y_hi, y_lo, w_hi, w_lo, h_hi, h_lo]
DATA_BYTES = PAYLOAD_BYTES - HEADER_BYTES  # 117 bytes = 936 pixels per transfer

# --- Init SPI ---
spi = spidev.SpiDev()
spi.open(SPI_BUS, SPI_DEV)
spi.max_speed_hz = SPI_SPEED_HZ
spi.mode = 0

# --- Init handshake ---
chip = gpiod.Chip(CHIP)
handshake = chip.get_line(HANDSHAKE_LINE)
handshake.request(consumer='display-bridge', type=gpiod.LINE_REQ_DIR_IN)


def wait_for_ready(timeout_s=10):
    t0 = time.monotonic()
    while int(handshake.get_value()) == 0:
        if time.monotonic() - t0 > timeout_s:
            return False
        time.sleep(0.01)
    return True


def capture_frame():
    """Read framebuffer, scale to 600x600, convert to 1-bit."""
    with open(FB_PATH, 'rb') as f:
        raw = f.read(FB_WIDTH * FB_HEIGHT * 4)  # RGBA 32bpp
    img = Image.frombytes('RGBA', (FB_WIDTH, FB_HEIGHT), raw)
    img = img.convert('L')                      # grayscale
    img = img.resize((DISPLAY_SIZE, DISPLAY_SIZE), Image.LANCZOS)
    img = img.convert('1')                      # 1-bit
    return img


def diff_frames(prev, curr):
    """Return bounding box (x, y, w, h) of changed region, or None if identical."""
    if prev is None:
        return (0, 0, DISPLAY_SIZE, DISPLAY_SIZE)

    prev_px = prev.load()
    curr_px = curr.load()

    min_x, min_y = DISPLAY_SIZE, DISPLAY_SIZE
    max_x, max_y = 0, 0
    changed = False

    for y in range(DISPLAY_SIZE):
        for x in range(DISPLAY_SIZE):
            if prev_px[x, y] != curr_px[x, y]:
                if x < min_x: min_x = x
                if y < min_y: min_y = y
                if x > max_x: max_x = x
                if y > max_y: max_y = y
                changed = True

    if not changed:
        return None

    return (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)


def encode_region(img, bbox):
    """Extract region from image as packed 1-bit bytes."""
    x, y, w, h = bbox
    region = img.crop((x, y, x + w, y + h))
    # Pack pixels row by row, MSB first
    pixels = list(region.getdata())
    packed = []
    for i in range(0, len(pixels), 8):
        byte = 0
        for bit in range(8):
            if i + bit < len(pixels) and pixels[i + bit]:
                byte |= (1 << (7 - bit))
        packed.append(byte)
    return packed


def send_region(bbox, pixel_data):
    """Split region into 128-byte SPI transfers and send each."""
    x, y, w, h = bbox

    offset = 0
    total = len(pixel_data)

    while offset < total:
        chunk = pixel_data[offset:offset + DATA_BYTES]
        chunk_len = len(chunk)

        # Header: marker, x, y, w, h (all as 16-bit big-endian)
        payload = [
            0x00, 0x00, 0x03,
            (x >> 8) & 0xFF, x & 0xFF,
            (y >> 8) & 0xFF, y & 0xFF,
            (w >> 8) & 0xFF, w & 0xFF,
            (h >> 8) & 0xFF, h & 0xFF,
        ]
        payload += chunk
        payload += [0x00] * (PAYLOAD_BYTES - len(payload))  # pad to 128 bytes

        if not wait_for_ready():
            print("Warning: Inkplate not ready, skipping chunk")
            return

        spi.xfer2(payload)
        offset += DATA_BYTES


def main():
    print(f"Display bridge started. Capturing {FB_WIDTH}x{FB_HEIGHT} → {DISPLAY_SIZE}x{DISPLAY_SIZE} @ {FPS}fps")
    prev_frame = None

    try:
        while True:
            t0 = time.monotonic()

            curr_frame = capture_frame()
            bbox = diff_frames(prev_frame, curr_frame)

            if bbox is not None:
                x, y, w, h = bbox
                print(f"  → change detected: ({x},{y}) {w}x{h}")
                pixel_data = encode_region(curr_frame, bbox)
                send_region(bbox, pixel_data)

            prev_frame = curr_frame

            elapsed = time.monotonic() - t0
            sleep_time = FRAME_INTERVAL - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\nExiting.")
    finally:
        spi.close()
        handshake.release()


if __name__ == '__main__':
    main()
