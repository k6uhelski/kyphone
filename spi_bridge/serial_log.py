#!/usr/bin/env python3
"""Stream Inkplate serial output to a log file. Run in background, tail -f the log."""
import serial, sys, datetime

PORT = "/dev/cu.usbserial-1130"
BAUD = 115200
LOG  = "/tmp/inkplate_serial.log"

with serial.Serial(PORT, BAUD, timeout=1) as ser, open(LOG, "a") as f:
    print(f"Logging {PORT} → {LOG}")
    while True:
        line = ser.readline()
        if line:
            ts  = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            txt = f"[{ts}] {line.decode('utf-8', errors='replace').rstrip()}\n"
            f.write(txt)
            f.flush()
            sys.stdout.write(txt)
            sys.stdout.flush()
