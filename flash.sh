#!/bin/bash
# Flash Inkplate firmware — compiles and uploads without Arduino IDE.
set -e

SKETCH="/Users/kyleuhelski/Desktop/kyphone/spi_bridge/Inkplate_SPI_Peripheral"
FQBN="Inkplate_Boards:esp32:Inkplate4TEMPERA"
PORT="/dev/cu.usbserial-1130"
LIBS="/Users/kyleuhelski/Documents/Arduino/libraries"
CACHE="/Users/kyleuhelski/Library/Caches/arduino/sketches"
ESPTOOL="/Users/kyleuhelski/Library/Arduino15/packages/Inkplate_Boards/tools/esptool_py/4.5.1/esptool"

echo "==> Compiling..."
arduino-cli compile --fqbn "$FQBN" --libraries "$LIBS" "$SKETCH"

# Find the compiled sketch dir (most recently modified)
SKETCH_DIR=$(ls -td "$CACHE"/*/  2>/dev/null | head -1)
BIN="$SKETCH_DIR/Inkplate_SPI_Peripheral.ino.bin"
BOOT="$SKETCH_DIR/Inkplate_SPI_Peripheral.ino.bootloader.bin"
PART="$SKETCH_DIR/Inkplate_SPI_Peripheral.ino.partitions.bin"

echo "==> Pausing serial logger..."
pkill -f serial_log.py 2>/dev/null || true
sleep 3

echo "==> Uploading at 115200 baud..."
"$ESPTOOL" --chip esp32 --port "$PORT" --baud 115200 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x10000 "$BIN" 0x1000 "$BOOT" 0x8000 "$PART"

echo "==> Waiting for board to boot..."
sleep 10

echo "==> Restarting serial logger..."
nohup python3 /Users/kyleuhelski/Desktop/kyphone/spi_bridge/serial_log.py \
  > /tmp/serial_log_stdout.txt 2>&1 &

echo "==> Done."
