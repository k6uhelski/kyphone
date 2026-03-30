"""
input_handler.py — USB keyboard reader for KyPhone navigation.

Finds the first keyboard-capable evdev device and calls on_key(keycode)
for every key press. Runs in a daemon thread so it doesn't block the app.

Install dependency on Radxa:
    pip3 install evdev
"""

import time
import threading
import evdev
from evdev import InputDevice, categorize, ecodes

NAV_KEYS = {
    'KEY_UP', 'KEY_DOWN', 'KEY_LEFT', 'KEY_RIGHT',
    'KEY_ENTER', 'KEY_BACKSPACE', 'KEY_ESC',
}


def find_keyboard():
    """Return the first InputDevice that looks like a keyboard."""
    for path in evdev.list_devices():
        try:
            dev = InputDevice(path)
            caps = dev.capabilities()
            keys = caps.get(ecodes.EV_KEY, [])
            if ecodes.KEY_ENTER in keys and ecodes.KEY_UP in keys:
                return dev
        except Exception:
            pass
    return None


class KeyboardHandler:
    def __init__(self, on_key):
        """
        on_key: callable(keycode: str) — called for each key press in NAV_KEYS.
        """
        self.on_key = on_key
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def _run(self):
        while True:
            device = None
            while device is None:
                device = find_keyboard()
                if device is None:
                    print("[keyboard] No keyboard found, retrying in 3s...")
                    time.sleep(3)
            print(f"[keyboard] Using: {device.name} ({device.path})")
            try:
                device.grab()
                print("[keyboard] Device grabbed (exclusive).")
            except OSError as e:
                print(f"[keyboard] Grab failed ({e}), reading anyway.")
            try:
                for event in device.read_loop():
                    if event.type == ecodes.EV_KEY:
                        key_event = categorize(event)
                        if key_event.keystate == key_event.key_down:
                            keycode = key_event.keycode
                            if isinstance(keycode, list):
                                keycode = keycode[0]
                            print(f"[keyboard] key: {keycode}")
                            if keycode in NAV_KEYS:
                                self.on_key(keycode)
            except OSError:
                print("[keyboard] Device disconnected, reconnecting...")
            finally:
                try:
                    device.ungrab()
                except Exception:
                    pass
            time.sleep(1)
