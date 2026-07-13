#!/usr/bin/env python3
"""Stream Inkplate serial output to a log file. Auto-reconnects after flash."""
import serial, sys, datetime, time

PORT = "/dev/cu.usbserial-1130"
BAUD = 115200
LOG  = "/tmp/inkplate_serial.log"

def timestamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]

with open(LOG, "a") as f:
    while True:
        try:
            ser = serial.Serial()
            ser.port = PORT
            ser.baudrate = BAUD
            ser.timeout = 1
            ser.dtr = False  # Don't assert DTR on open (prevents ESP32 reset)
            ser.rts = False  # Don't assert RTS on open
            ser.open()

            msg = f"[{timestamp()}] --- connected to {PORT} ---\n"
            f.write(msg); f.flush(); sys.stdout.write(msg); sys.stdout.flush()

            while True:
                line = ser.readline()
                if line:
                    txt = f"[{timestamp()}] {line.decode('utf-8', errors='replace').rstrip()}\n"
                    f.write(txt); f.flush()
                    sys.stdout.write(txt); sys.stdout.flush()
        except serial.SerialException:
            try: ser.close()
            except: pass
            msg = f"[{timestamp()}] --- port lost, retrying in 5s ---\n"
            f.write(msg); f.flush(); sys.stdout.write(msg); sys.stdout.flush()
            time.sleep(5)
