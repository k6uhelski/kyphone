import serial
import time

# --- Configure this ---
SERIAL_PORT = '/dev/cu.usbserial-120' # Make sure this is correct
BAUD_RATE = 115200
IMAGE_FILE = 'image.bin'
# --------------------

try:
    with open(IMAGE_FILE, 'rb') as f:
        image_data = f.read()
    print(f"Loaded {len(image_data)} byte image from '{IMAGE_FILE}'.")

    # Set a timeout for reading operations
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"Connected to Inkplate. Sending image every 10 seconds...")
    time.sleep(2) # Wait for the Arduino to reset after connection

    while True:
        print("\n--- Sending image... ---")
        ser.write(image_data)
        print("Done sending. Now listening for response from Arduino...")
        
        # Give the Arduino a moment to process and send back serial data
        time.sleep(0.5)
        
        # --- NEW: Read any available lines from the Arduino ---
        while ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8').strip()
                if line: # Only print if the line is not empty
                    print(f"  [Arduino Says]: {line}")
            except UnicodeDecodeError:
                print("  [Arduino Sent]: (non-UTF-8 data)")
        
        print("--- Finished listening. Waiting for next cycle... ---")
        time.sleep(9.5)

except Exception as e:
    print(f"An error occurred: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print("\nSerial port closed.")