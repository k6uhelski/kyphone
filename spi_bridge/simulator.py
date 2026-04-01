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

CLOCK_FONT = 'futura'  # swap this to try other fonts

_FONT_CACHE = {}


def _get_font(px_size, bold=False, clock=False):
    key = (px_size, bold, clock)
    if key not in _FONT_CACHE:
        name = CLOCK_FONT if clock else 'courier'
        _FONT_CACHE[key] = pygame.font.SysFont(name, px_size, bold=False)
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

    def _text_centered(self, text, y, text_size, color=BLACK, clock=False):
        if clock:
            font = _get_font(text_size * 8, bold=True, clock=True)
            w = font.size(str(text))[0]
            x = (self.WIDTH - w) // 2
            self._surface.blit(font.render(str(text), True, color), (x, y))
        else:
            w = len(str(text)) * self._char_w(text_size)
            x = (self.WIDTH - w) // 2
            self._text(text, x, y, text_size, color)

    def _line(self, y):
        pygame.draw.line(self._surface, BLACK, (0, y), (self.WIDTH, y), 1)

    def _wrap_lines(self, text, text_size, max_px):
        """Break text into lines fitting within max_px width."""
        char_w = self._char_w(text_size)
        max_chars = max_px // char_w
        words = text.split(' ')
        lines, current = [], ''
        for word in words:
            if not current:
                current = word
            elif len(current) + 1 + len(word) <= max_chars:
                current += ' ' + word
            else:
                lines.append(current)
                current = word
        if current:
            lines.append(current)
        return lines or ['']

    def _draw(self, command):
        self._surface.fill(WHITE)
        if '|' in command:
            prefix, rest = command.split('|', 1)
        else:
            prefix, rest = command, ''

        if prefix in ('HOME', 'HOME_FAST'):
            self._draw_home(rest)
        elif prefix in ('MSG_LIST', 'MSG_LIST_FAST'):
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
        try:
            unread = int(parts[2]) if len(parts) > 2 else 0
        except ValueError:
            unread = 0
        try:
            home_sel = int(parts[3]) if len(parts) > 3 else -1
        except ValueError:
            home_sel = -1

        # Status bar
        self._text('KyPhone', 10, 8, 2)

        # Clock + date vertically centered in space above button row (y=35..510)
        total_h = 80 + 24 + 24
        start_y = 35 + (475 - total_h) // 2

        self._text_centered(time_str, start_y, 10, clock=True)
        self._text_centered(date_str, start_y + 80 + 24, 3)

        # YAP / CHILL inline labels — YAP spans TEXT+CALL, CHILL spans READ+LISTEN
        # btn_positions = [0, 150, 308, 458], btn_w = 142
        yap_rx   = 150 + 142   # right edge of CALL = 292
        chill_lx = 308          # left edge of READ
        line_y = 526
        pad = 4
        for label, lx, rx in [('YAP', 0, yap_rx), ('CHILL', chill_lx, 599)]:
            font = _get_font(11)
            lw = font.size(label)[0]
            label_x = lx + (rx - lx - lw) // 2
            label_y = line_y - 9
            # End caps
            pygame.draw.line(self._surface, BLACK, (lx, line_y - 4), (lx, line_y + 4), 1)
            pygame.draw.line(self._surface, BLACK, (rx, line_y - 4), (rx, line_y + 4), 1)
            # Line left of label
            pygame.draw.line(self._surface, BLACK, (lx, line_y), (label_x - pad, line_y), 1)
            # Label
            self._surface.blit(font.render(label, True, BLACK), (label_x, label_y))
            # Line right of label
            pygame.draw.line(self._surface, BLACK, (label_x + lw + pad, line_y), (rx, line_y), 1)

        # 4 buttons — 8px gap between YAP and CHILL groups
        buttons = ['TEXT', 'CALL', 'READ', 'LISTEN']
        btn_positions = [0, 150, 308, 458]
        btn_w, btn_h, btn_y = 142, 65, 535
        for i, (label, bx) in enumerate(zip(buttons, btn_positions)):
            cw = len(label) * self._char_w(2)
            lx = bx + (btn_w - cw) // 2
            ly = btn_y + (btn_h - 16) // 2
            selected = i == home_sel and home_sel >= 0
            if selected:
                pygame.draw.rect(self._surface, BLACK, (bx, btn_y, btn_w, btn_h))
                self._text(label, lx, ly, 2, WHITE)
            else:
                pygame.draw.rect(self._surface, BLACK, (bx, btn_y, btn_w, btn_h), 2)
                self._text(label, lx, ly, 2, BLACK)

            # Unread badge on TEXT button (index 0)
            if i == 0 and unread > 0:
                badge_size = 24
                pygame.draw.rect(self._surface, WHITE if selected else BLACK,
                                 (bx + 2, btn_y + 2, badge_size, badge_size))
                badge_label = str(min(unread, 9))
                bx2 = bx + 2 + (badge_size - self._char_w(2)) // 2
                by2 = btn_y + 2 + (badge_size - 16) // 2
                self._text(badge_label, bx2, by2, 2, BLACK if selected else WHITE)

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

        self._text_centered(name, 10, 3)
        self._line(46)

        y = 60
        ts = 3
        line_h = ts * 8 + 12
        margin = 20
        max_w = self.WIDTH - margin * 2
        for msg in msgs:
            if len(msg) >= 2 and msg[1] == ':':
                align, body = msg[0], msg[2:]
            else:
                align, body = 'R', msg
            for line in self._wrap_lines(body, ts, max_w):
                if align == 'Y':
                    self._text(line, margin, y, ts)
                else:
                    w = len(line) * self._char_w(ts)
                    self._text(line, self.WIDTH - w - margin, y, ts)
                y += line_h

    def _draw_sms(self, text):
        if '|' in text:
            sender, body = text.split('|', 1)
        else:
            sender, body = text, ''
        self._text(sender, 10, 10, 3)
        self._text(body,   10, 60, 4)
