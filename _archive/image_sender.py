import serial
from PIL import Image, ImageOps
import time
from pynput import keyboard # <-- Import the new library

# --- CONFIGURATION & HELPER FUNCTIONS (These are all the same) ---
SERIAL_PORT = '/dev/cu.usbserial-120'
BAUD_RATE = 115200

def send_image_data(ser, image_data, command):
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

def update_full_screen(ser, image):
    if image.size != (600, 600):
        image = image.resize((600, 600))
    image_1bit = image.convert('1')
    final_image = ImageOps.invert(image_1bit)
    byte_data = final_image.tobytes()
    print("\nSending full screen update...")
    send_image_data(ser, byte_data, "IMG_DATA")
    print("Update complete.")

def show_screen(ser, screen_name):
    try:
        image_path = f"assets/{screen_name}.png"
        print(f"Loading {image_path}...")
        image = Image.open(image_path)
        update_full_screen(ser, image)
    except FileNotFoundError:
        print(f"Error: Could not find {image_path}")

# --- NEW KEYBOARD HANDLER using pynput ---
def on_press(key):
    """Handles key press events."""
    try:
        # Get the character for the pressed key
        char = key.char
        if char == '1':
            show_screen(ser, 'intro_screen')
        elif char == '2':
            show_screen(ser, 'home_screen')
        elif char == '3':
            show_screen(ser, 'schedule_screen')
        elif char == 'q':
            # Stop the listener
            print("\n'q' pressed, exiting.")
            return False
    except AttributeError:
        # This handles special keys like 'shift', 'ctrl', etc.
        pass

# --- MAIN SCRIPT ---
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

    show_screen(ser, "intro_screen")

    print("\nListening for keyboard input...")
    print("Press '1' for Intro, '2' for Home, '3' for Schedule. Press 'q' to quit.")
    
    # Start the listener
    with keyboard.Listener(on_press=on_press) as listener:
        listener.join()

except (serial.SerialException, ConnectionError, ValueError) as e:
    print(f"❌ An error occurred: {e}")
finally:
    if ser and ser.is_open:
        ser.close()
        print("\nDisconnected from Inkplate.")