"""
input_handler.py — USB keyboard reader for KyPhone navigation.

Finds the first keyboard-capable evdev device and calls on_key(keycode)
for every key press. Runs in a daemon thread so it doesn't block the app.

Keycodes sent to on_key():
  Navigation : KEY_UP / KEY_DOWN / KEY_LEFT / KEY_RIGHT / KEY_ENTER /
               KEY_BACKSPACE / KEY_ESC / KEY_TAB
  Printable  : CHAR:<character>   e.g. CHAR:a  CHAR:+  CHAR:

Install dependency on Radxa:
    pip3 install evdev
"""

import time
import threading
import evdev
from evdev import InputDevice, categorize, ecodes

NAV_KEYS = {
    'KEY_UP', 'KEY_DOWN', 'KEY_LEFT', 'KEY_RIGHT',
    'KEY_ENTER', 'KEY_BACKSPACE', 'KEY_ESC', 'KEY_TAB',
}

SHIFT_KEYS = {'KEY_LEFTSHIFT', 'KEY_RIGHTSHIFT'}

# Unshifted punctuation / special chars
_CHAR_UNSHIFT = {
    'KEY_SPACE':       ' ',
    'KEY_PERIOD':      '.',
    'KEY_COMMA':       ',',
    'KEY_APOSTROPHE':  "'",
    'KEY_MINUS':       '-',
    'KEY_EQUAL':       '=',
    'KEY_SLASH':       '/',
    'KEY_SEMICOLON':   ';',
    'KEY_GRAVE':       '`',
    'KEY_LEFTBRACE':   '[',
    'KEY_RIGHTBRACE':  ']',
    'KEY_BACKSLASH':   '\\',
}

# Shifted versions of the above + shift+digit symbols
_CHAR_SHIFT = {
    'KEY_SPACE':       ' ',
    'KEY_PERIOD':      '>',
    'KEY_COMMA':       '<',
    'KEY_APOSTROPHE':  '"',
    'KEY_MINUS':       '_',
    'KEY_EQUAL':       '+',
    'KEY_SLASH':       '?',
    'KEY_SEMICOLON':   ':',
    'KEY_GRAVE':       '~',
    'KEY_LEFTBRACE':   '{',
    'KEY_RIGHTBRACE':  '}',
    'KEY_BACKSLASH':   '|',
    'KEY_1': '!', 'KEY_2': '@', 'KEY_3': '#',
    'KEY_4': '$', 'KEY_5': '%', 'KEY_6': '^',
    'KEY_7': '&', 'KEY_8': '*', 'KEY_9': '(',
    'KEY_0': ')',
}


def _keycode_to_char(keycode, shift):
    """Return printable char for keycode, or None if not printable."""
    table = _CHAR_SHIFT if shift else _CHAR_UNSHIFT
    c = table.get(keycode)
    if c:
        return c
    # Single letter/digit: KEY_A … KEY_Z, KEY_0 … KEY_9
    if keycode.startswith('KEY_') and len(keycode) == 5:
        ch = keycode[4]
        if ch.isalpha():
            return ch.upper() if shift else ch.lower()
        if ch.isdigit() and not shift:
            return ch
    return None


def find_keyboard():
    """Return the keyboard InputDevice with the most supported keys."""
    candidates = []
    for path in evdev.list_devices():
        try:
            dev  = InputDevice(path)
            caps = dev.capabilities()
            keys = caps.get(ecodes.EV_KEY, [])
            if ecodes.KEY_ENTER in keys and ecodes.KEY_UP in keys and ecodes.KEY_A in keys:
                candidates.append((len(keys), dev))
        except Exception:
            pass
    if candidates:
        candidates.sort(key=lambda x: x[0], reverse=True)
        return candidates[0][1]
    return None


class KeyboardHandler:
    def __init__(self, on_key):
        """on_key(keycode: str) — called for each key press."""
        self.on_key  = on_key
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

            shift_held = False
            try:
                for event in device.read_loop():
                    if event.type != ecodes.EV_KEY:
                        continue
                    key_event = categorize(event)
                    keycode   = key_event.keycode
                    if isinstance(keycode, list):
                        keycode = keycode[0]

                    # Track shift
                    if keycode in SHIFT_KEYS:
                        shift_held = (key_event.keystate != key_event.key_up)
                        continue

                    if key_event.keystate == key_event.key_down:
                        if keycode in NAV_KEYS:
                            self.on_key(keycode)
                        else:
                            char = _keycode_to_char(keycode, shift_held)
                            if char:
                                self.on_key(f'CHAR:{char}')

            except OSError:
                print("[keyboard] Device disconnected, reconnecting...")
            finally:
                try:
                    device.ungrab()
                except Exception:
                    pass
            time.sleep(1)
