"""
simulator.py — KyPhone display simulator for local development.

Renders KyPhone screen commands in a 600x600 pygame window, mimicking
the Inkplate 4 TEMPERA layout. Keyboard input (arrow keys, Enter, Esc,
Backspace) maps to the same keycodes as the evdev handler.

Install: pip3 install pygame
Run:     python3 kyphone_app.py --sim
"""

import sys
import threading
import pygame

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

_FONT_CACHE = {}


def _get_font(px_size, bold=False):
    key = (px_size, bold)
    if key not in _FONT_CACHE:
        _FONT_CACHE[key] = pygame.font.SysFont('courier', px_size, bold=bold)
    return _FONT_CACHE[key]


class Simulator:
    """
    Mimics the Inkplate renderer. Call render(command) from any thread;
    call run_loop() from the main thread to drive the pygame event loop.
    """

    WIDTH = 600
    HEIGHT = 600

    # Inkplate textSize N → char cell: width=6N px, height=8N px
    # We approximate with a Courier monospace font at height 8N px.

    KEY_MAP = {
        pygame.K_UP:        'KEY_UP',
        pygame.K_DOWN:      'KEY_DOWN',
        pygame.K_LEFT:      'KEY_LEFT',
        pygame.K_RIGHT:     'KEY_RIGHT',
        pygame.K_RETURN:    'KEY_ENTER',
        pygame.K_BACKSPACE: 'KEY_BACKSPACE',
        pygame.K_ESCAPE:    'KEY_ESC',
    }

    def __init__(self, on_key):
        """on_key(keycode: str) called on each navigation key press."""
        self.on_key = on_key
        self._lock = threading.Lock()
        self._pending = None
        self._surface = None
        self._ready = False

    def init(self):
        pygame.init()
        self._surface = pygame.display.set_mode((self.WIDTH, self.HEIGHT))
        self._ready = True
        pygame.display.set_caption('KyPhone Simulator')
        self._surface.fill(WHITE)
        pygame.display.flip()

    def render(self, command):
        """Queue a screen command for rendering (thread-safe)."""
        if not self._ready:
            return
        with self._lock:
            self._pending = command
        pygame.event.post(pygame.event.Event(pygame.USEREVENT))

    def run_loop(self):
        """Pygame event loop — must be called from the main thread."""
        clock = pygame.time.Clock()
        while True:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    pygame.quit()
                    sys.exit(0)
                elif event.type == pygame.USEREVENT:
                    with self._lock:
                        cmd = self._pending
                    if cmd:
                        self._draw(cmd)
                elif event.type == pygame.KEYDOWN:
                    keycode = self.KEY_MAP.get(event.key)
                    if keycode:
                        # Run in thread so key handler doesn't block event loop
                        threading.Thread(
                            target=self.on_key, args=(keycode,), daemon=True
                        ).start()
            clock.tick(60)

    # ── Internal draw helpers ─────────────────────────────────────────

    def _font(self, text_size, bold=False):
        return _get_font(text_size * 8, bold)

    def _char_w(self, text_size):
        return text_size * 6

    def _text(self, text, x, y, text_size, color=BLACK, bold=False):
        font = self._font(text_size, bold)
        img = font.render(str(text), True, color)
        self._surface.blit(img, (x, y))

    def _text_centered(self, text, y, text_size, color=BLACK):
        w = len(str(text)) * self._char_w(text_size)
        x = (self.WIDTH - w) // 2
        self._text(text, x, y, text_size, color)

    def _line(self, y):
        pygame.draw.line(self._surface, BLACK, (0, y), (self.WIDTH, y), 1)

    def _draw(self, command):
        self._surface.fill(WHITE)
        if '|' in command:
            prefix, rest = command.split('|', 1)
        else:
            prefix, rest = command, ''

        if prefix in ('HOME', 'HOME_FAST'):
            self._draw_home(rest)
        elif prefix == 'MSG_LIST':
            self._draw_msg_list(rest)
        elif prefix == 'MSG_THREAD':
            self._draw_msg_thread(rest)
        else:
            self._draw_sms(command)

        pygame.display.flip()

    # ── Screen renderers ──────────────────────────────────────────────

    def _draw_home(self, data):
        parts = data.split('|')
        time_str  = parts[0] if len(parts) > 0 else ''
        date_str  = parts[1] if len(parts) > 1 else ''
        notif_str = parts[2] if len(parts) > 2 else ''
        yap_sel   = (parts[3] == '1') if len(parts) > 3 else False
        has_notif = len(notif_str) > 0

        # Status bar
        self._text('KyPhone', 10, 8, 2)
        self._line(34)

        # Clock + date vertically centered in remaining space (y=35..600)
        total_h = 80 + 24 + 24
        start_y = 35 + (565 - total_h) // 2

        self._text_centered(time_str, start_y, 10)
        self._text_centered(date_str, start_y + 80 + 24, 3)

        if has_notif:
            notif_y = start_y + 80 + 24 + 40
            self._line(notif_y)
            self._text(notif_str, 20, notif_y + 10, 3)

        # YAP button — bottom left
        yap_cx, yap_cy, yap_r = 80, 545, 50
        if yap_sel:
            pygame.draw.circle(self._surface, BLACK, (yap_cx, yap_cy), yap_r)
            self._text('YAP', yap_cx - 27, yap_cy - 12, 3, WHITE)
        else:
            pygame.draw.circle(self._surface, BLACK, (yap_cx, yap_cy), yap_r, 2)
            self._text('YAP', yap_cx - 27, yap_cy - 12, 3, BLACK)

    def _draw_msg_list(self, data):
        parts = data.split('|')
        sel = int(parts[0]) if parts and parts[0].isdigit() else 0
        entries = parts[1:] if parts and parts[0].isdigit() else parts

        self._text('Messages', 20, 10, 3)
        self._line(46)

        y = 60
        for i, entry in enumerate(entries):
            if '\xb7' in entry:
                name, preview = entry.split('\xb7', 1)
            else:
                name, preview = entry, ''

            if i == sel:
                pygame.draw.rect(self._surface, BLACK, (0, y - 4, self.WIDTH, 84))
                self._text(name,    20, y,      3, WHITE)
                self._text(preview, 20, y + 34, 2, WHITE)
            else:
                self._text(name,    20, y,      3, BLACK)
                self._text(preview, 20, y + 34, 2, BLACK)

            y += 90
            self._line(y - 6)

    def _draw_msg_thread(self, data):
        parts = data.split('|')
        name = parts[0] if parts else ''
        msgs = parts[1:] if len(parts) > 1 else []

        self._text(name, 20, 10, 3)
        self._line(46)

        y = 60
        for msg in msgs:
            self._text(msg, 20, y, 2)
            y += 30

    def _draw_sms(self, text):
        if '|' in text:
            sender, body = text.split('|', 1)
        else:
            sender, body = text, ''
        self._text(sender, 10, 10, 3)
        self._text(body,   10, 60, 4)
