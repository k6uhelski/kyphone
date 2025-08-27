import serial
from PIL import Image, ImageOps
import time
import sys

# --- CONFIGURATION ---
SERIAL_PORT = '/dev/cu.usbserial-1120'
BAUD_RATE = 115200
# ---------------------

def send_image_data(ser, image_data, command):
    """Handles the low-level, chunked data transfer with ACK confirmation."""
    ser.write(f"{command}\n".encode())
    response = ser.readline().decode('utf-8').strip()
    if not response.startswith("READY_FOR"):
        raise ConnectionError(f"Handshake failed. Expected READY_FOR..., got '{response}'")
    
    time.sleep(0.1)
    
    CHUNK_SIZE = 4096
    total_bytes = len(image_data)
    bytes_sent = 0
    while bytes_sent < total_bytes:
        chunk = image_data[bytes_sent : bytes_sent + CHUNK_SIZE]
        ser.write(chunk)
        response = ser.readline().decode('utf-8').strip()
        if response != "ACK":
            raise ConnectionError(f"Transfer error. Expected 'ACK', got '{response}'")
        bytes_sent += len(chunk)
    print(f"Sent {bytes_sent} bytes successfully for command '{command}'.")

def update_full_screen(ser, image):
    """Prepares a 600x600 image and sends it for a full screen refresh."""
    if image.size != (600, 600):
        print(f"Warning: Image is not 600x600. Resizing...")
        image = image.resize((600, 600))
    
    image_1bit = image.convert('1')
    final_image = ImageOps.invert(image_1bit)
    byte_data = final_image.tobytes()
    
    print("Sending full screen update...")
    send_image_data(ser, byte_data, "IMG_DATA")
    print("Full screen update complete.")

def update_partial_area(ser, image, x, y):
    """Prepares a smaller image and sends it for a partial update at a specific coordinate."""
    w, h = image.size
    image_1bit = image.convert('1')
    final_image = ImageOps.invert(image_1bit)
    byte_data = final_image.tobytes()
    
    command = f"PARTIAL_DATA,{x},{y},{w},{h}"

    print(f"Sending partial update to position ({x}, {y})...")
    send_image_data(ser, byte_data, command)
    print("Partial update complete.")


# --- Main Script Execution ---
if __name__ == "__main__":
    # Check if a filename was provided as an argument
    if len(sys.argv) < 2:
        print("Usage: python3 image_sender.py <image_filename>")
        print("Example: python3 image_sender.py assets/test_pic1.png")
        sys.exit(1)

    image_path = sys.argv[1]

    ser = None
    try:
        print(f"Connecting to {SERIAL_PORT}...")
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=10)
        time.sleep(2)
        ser.write(b'START\n')
        response = ser.readline().decode('utf-8').strip()
        if response != "READY":
            raise ConnectionError(f"Initial handshake failed. Expected 'READY', got '{response}'")
        print("✅ Connection successful. Session started.")

        print(f"\n--- Sending Full Screen Background from '{image_path}' ---")
        full_image = Image.open(image_path)
        update_full_screen(ser, full_image)
        
        print("\nScript finished.")

    except (serial.SerialException, ConnectionError, FileNotFoundError, ValueError) as e:
        print(f"❌ An error occurred: {e}")
    finally:
        if ser and ser.is_open:
            ser.close()
            print("Disconnected from Inkplate.")