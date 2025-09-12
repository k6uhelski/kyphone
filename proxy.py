import socket
import serial
import time

# --- CONFIGURATION ---
SERIAL_PORT = '/dev/cu.usbserial-120'
BAUD_RATE = 115200
HOST = '127.0.0.1'  # Localhost
PORT = 65432        # The port to listen on
REFRESH_COOLDOWN = 10 # Seconds to wait for the Inkplate to refresh
# ---------------------

print("--- KyPhone Proxy Server ---")

try:
    print(f"Connecting to Inkplate on {SERIAL_PORT}...")
    inkplate_serial = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=15)
    print("✅ Connected to Inkplate.")

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((HOST, PORT))
    server_socket.listen()
    print(f"Listening for emulator on network port {PORT}...")

    while True:
        # 1. Accept a connection from the Android app.
        conn, addr = server_socket.accept()
        with conn:
            print(f"\n✅ Emulator connected from {addr}")
            
            # 2. Forward all data from this connection to the Inkplate.
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                inkplate_serial.write(data)
                print(f"Forwarded {len(data)} bytes to Inkplate.")
        print("✅ Image data forwarded.")

        # --- THIS IS THE FIX ---
        # 3. Enforce a mandatory cool-down period to allow the slow E Ink
        #    screen to finish its refresh before we process the next image.
        print(f"Starting {REFRESH_COOLDOWN} second cool-down for Inkplate refresh...")
        time.sleep(REFRESH_COOLDOWN)
        print("✅ Cool-down finished. Ready for next image.")
        # -----------------------

except KeyboardInterrupt:
    print("\nShutting down server.")
except Exception as e:
    print(f"\nAn error occurred: {e}")
finally:
    if 'server_socket' in locals():
        server_socket.close()
    if 'inkplate_serial' in locals() and inkplate_serial.is_open:
        inkplate_serial.close()
    print("Connections closed.")

