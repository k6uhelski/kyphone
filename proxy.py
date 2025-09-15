import socket
import serial
import time

# --- FINAL PROXY w/ ACK ---
SERIAL_PORT = '/dev/cu.usbserial-120'
BAUD_RATE = 115200
HOST = '127.0.0.1'
PORT = 65432
REFRESH_COOLDOWN = 3
# --------------------------

print("--- KyPhone Proxy Server (ACK Edition) ---")
server_socket = None
inkplate_serial = None
try:
    print(f"Connecting to Inkplate on {SERIAL_PORT}...")
    inkplate_serial = serial.Serial(SERIAL_PORT, BAUD_RATE)
    print("✅ Connected to Inkplate.")
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((HOST, PORT))
    server_socket.listen()
    print(f"Listening for emulator on network port {PORT}...")
    while True:
        conn, addr = server_socket.accept()
        print(f"\n✅ Connection accepted from {addr}")
        try:
            # Wrap the socket connection in a file-like object for easy line reading
            rfile = conn.makefile('rb')
            
            # Read the first line, which is our version string
            version_line = rfile.readline().decode('utf-8').strip()
            print(f"📱 App version reported: '{version_line}'")
            
            # The rest of the data in the stream is the image
            image_data = rfile.read()
            total_bytes = len(image_data)
            
            if total_bytes > 0:
                inkplate_serial.write(image_data)
                print(f"✅ Forwarded {total_bytes} bytes to Inkplate.")
            else:
                print("⚠️ Received 0 bytes of image data.")
            
            # NEW: Send an acknowledgment (a single byte) back to the client
            print("Sending OK acknowledgment to app...")
            conn.sendall(b'\x01')

        except Exception as e:
            print(f"🔥 Error during communication: {e}")
        finally:
            conn.close() # Ensure connection is always closed
        
        print(f"Starting {REFRESH_COOLDOWN} second cool-down...")
        time.sleep(REFRESH_COOLDOWN)
        print("✅ Cool-down finished. Ready for next connection.")
except Exception as e:
    print(f"🔥 A FATAL error occurred: {e}")
finally:
    if server_socket:
        server_socket.close()
    if inkplate_serial and inkplate_serial.is_open:
        inkplate_serial.close()
    print("Server shutdown complete.")