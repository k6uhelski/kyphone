import socket
import serial
import select

# --- CONFIGURATION ---
SERIAL_PORT = '/dev/cu.usbserial-1140'
BAUD_RATE = 115200
HOST = '127.0.0.1'
PORT = 65432
IMAGE_SIZE = 45000
# ---------------------

print("--- KyPhone Final Proxy (ADB Logic Removed) ---")

# ++ THIS IS THE CORRECTED LINE ++
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.setblocking(False)
server_socket.bind((HOST, PORT))
server_socket.listen(5)

inkplate_serial = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0)

print(f"✅ Listening for app on {HOST}:{PORT}")
print(f"✅ Listening for Inkplate on {SERIAL_PORT}")

inputs = [server_socket, inkplate_serial]
client_socket = None
inkplate_ready_for_image = True
image_buffer = b""

try:
    while inputs:
        readable, _, _ = select.select(inputs, [], [])
        for s in readable:
            if s is server_socket:
                conn, addr = server_socket.accept()
                print(f"\n✅ App connected from {addr}")
                conn.setblocking(False)
                inputs.append(conn)
                client_socket = conn
                inkplate_ready_for_image = True
                image_buffer = b""
                print("Proxy -> App: Sending initial ACK to prime the pump.")
                client_socket.sendall(b'ACK\n')
            
            elif s is inkplate_serial:
                line = inkplate_serial.readline().decode('utf-8').strip()
                if line:
                    if line == "ACK":
                        print("Inkplate -> App: ACK received. Ready for next image.")
                        inkplate_ready_for_image = True
                        if client_socket:
                            client_socket.sendall(b'ACK\n')
                    else:
                        # ++ MODIFIED BLOCK ++
                        # We just print and forward. No more subprocess.
                        print(f"Inkplate -> App: {line}")
                        
                        # This line is now the primary action.
                        # It sends the coordinates to the Android app.
                        if client_socket:
                           client_socket.sendall(line.encode('utf-8') + b'\n')
                        # -- END OF MODIFIED BLOCK --

            elif s is client_socket:
                if not inkplate_ready_for_image:
                    continue

                data_chunk = s.recv(4096)
                if data_chunk:
                    image_buffer += data_chunk
                    if len(image_buffer) >= IMAGE_SIZE:
                        print(f"App -> Inkplate: Forwarding {len(image_buffer)} bytes...")
                        inkplate_serial.write(image_buffer)
                        image_buffer = b""
                        inkplate_ready_for_image = False
                else:
                    print("\n⚠️ App disconnected.")
                    inputs.remove(s)
                    s.close()
                    client_socket = None
                    image_buffer = b""

except KeyboardInterrupt:
    print("\nShutting down server.")
finally:
    if client_socket:
        client_socket.close()
    server_socket.close()
    inkplate_serial.close()
    print("Connections closed.")