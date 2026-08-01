"""
trackpad_handler.py — BLE trackpad reader for KyPhone navigation.

Finds the first relative-pointing evdev device (BTN_LEFT + REL_X/REL_Y) and
translates swipes into discrete nav keys, and clicks into Enter.

A "swipe" is a continuous burst of motion events with no pause longer than
GESTURE_GAP_SECONDS between them. At most one KEY_UP/DOWN/LEFT/RIGHT fires
per burst, the moment accumulated motion in the dominant axis crosses
SWIPE_THRESHOLD — further motion in that same burst is ignored. A new burst
(finger lifted and swiped again) starts only after a real pause, which is
what lets you fire a second step. This is deliberately not a fixed-rate
cooldown: a single slow/long swipe would out-last a fixed cooldown window
and could double-fire mid-gesture, which a burst-boundary reset avoids.

No right-click mapping: the KyPhone UI is index-based, so reaching and
activating the '<' back button only needs Enter on the already-navigable
header row, not a second button.

Keycodes sent to on_key(): KEY_UP / KEY_DOWN / KEY_LEFT / KEY_RIGHT / KEY_ENTER

Install dependency on Radxa:
    pip3 install evdev
"""

import time
import threading
import evdev
from evdev import InputDevice, ecodes

SWIPE_THRESHOLD = 15
GESTURE_GAP_SECONDS = 0.15  # idle gap between REL events that marks a new swipe


def find_trackpad():
    """Return the first evdev device with left-click + relative X/Y motion."""
    for path in evdev.list_devices():
        try:
            dev  = InputDevice(path)
            caps = dev.capabilities()
            keys = caps.get(ecodes.EV_KEY, [])
            rels = caps.get(ecodes.EV_REL, [])
            if ecodes.BTN_LEFT in keys and ecodes.REL_X in rels and ecodes.REL_Y in rels:
                return dev
        except Exception:
            pass
    return None


class TrackpadHandler:
    def __init__(self, on_key):
        """on_key(keycode: str) — called for each nav step / click."""
        self.on_key   = on_key
        self._thread  = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def _run(self):
        while True:
            device = None
            while device is None:
                device = find_trackpad()
                if device is None:
                    print("[trackpad] No trackpad found, retrying in 3s...")
                    time.sleep(3)
            print(f"[trackpad] Using: {device.name} ({device.path})")

            accum_x = accum_y = 0
            fired_this_burst = False
            last_event_ts = 0.0
            try:
                for event in device.read_loop():
                    if event.type == ecodes.EV_REL:
                        now = time.monotonic()
                        if now - last_event_ts > GESTURE_GAP_SECONDS:
                            accum_x = accum_y = 0
                            fired_this_burst = False
                        last_event_ts = now

                        if event.code == ecodes.REL_X:
                            accum_x += event.value
                        elif event.code == ecodes.REL_Y:
                            accum_y += event.value

                        if not fired_this_burst and (
                            abs(accum_x) >= SWIPE_THRESHOLD or abs(accum_y) >= SWIPE_THRESHOLD
                        ):
                            if abs(accum_x) > abs(accum_y):
                                key = 'KEY_RIGHT' if accum_x > 0 else 'KEY_LEFT'
                            else:
                                key = 'KEY_DOWN' if accum_y > 0 else 'KEY_UP'
                            print(f"[trackpad] fired {key} (accum_x={accum_x} accum_y={accum_y})")
                            self.on_key(key)
                            fired_this_burst = True

                    elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_LEFT:
                        if event.value == 1:
                            print("[trackpad] fired KEY_ENTER (click)")
                            self.on_key('KEY_ENTER')

            except OSError:
                print("[trackpad] Device disconnected, reconnecting...")
            time.sleep(1)
