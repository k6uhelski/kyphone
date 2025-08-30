from pynput import mouse

def on_click(x, y, button, pressed):
    """This function is called every time a mouse button is pressed or released."""
    if pressed:
        print(f"Mouse clicked at ({x}, {y}) with {button}")

print("Listening for mouse clicks... Press Ctrl+C to stop.")

# Create a listener that calls the on_click function for each mouse event
with mouse.Listener(on_click=on_click) as listener:
    listener.join()