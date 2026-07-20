"""
simulator.py — KyPhone display simulator for local development.

Renders KyPhone OS screen commands in a 600x600 pygame window.
Keyboard input maps to the same keycodes as the evdev handler.

Install: pip3 install pygame
Run:     python3 spi_bridge/kyphone_os.py --sim
"""

import sys
import threading
import pygame

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

CLOCK_FONT = 'futura'

_FONT_CACHE = {}


def _get_font(px_size, bold=False, clock=False):
    key = (px_size, bold, clock)
    if key not in _FONT_CACHE:
        name = CLOCK_FONT if clock else 'courier'
        _FONT_CACHE[key] = pygame.font.SysFont(name, px_size, bold=bold)
    return _FONT_CACHE[key]


class Simulator:
    WIDTH  = 600
    HEIGHT = 600

    # Inkplate textSize N → char cell: width=6N px, height=8N px

    KEY_MAP = {
        pygame.K_UP:        'KEY_UP',
        pygame.K_DOWN:      'KEY_DOWN',
        pygame.K_LEFT:      'KEY_LEFT',
        pygame.K_RIGHT:     'KEY_RIGHT',
        pygame.K_RETURN:    'KEY_ENTER',
        pygame.K_BACKSPACE: 'KEY_BACKSPACE',
        pygame.K_ESCAPE:    'KEY_ESC',
        pygame.K_TAB:       'KEY_TAB',
    }

    def __init__(self, on_key):
        self.on_key   = on_key
        self._lock    = threading.Lock()
        self._pending = None
        self._surface = None
        self._ready   = False

    def init(self):
        pygame.init()
        self._surface = pygame.display.set_mode((self.WIDTH, self.HEIGHT))
        self._ready   = True
        pygame.display.set_caption('KyPhone Simulator')
        self._surface.fill(WHITE)
        pygame.display.flip()

    def render(self, command):
        if not self._ready:
            return
        with self._lock:
            self._pending = command
        pygame.event.post(pygame.event.Event(pygame.USEREVENT))

    def run_loop(self):
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
                        threading.Thread(
                            target=self.on_key, args=(keycode,), daemon=True
                        ).start()
                    elif event.unicode and event.unicode.isprintable() and len(event.unicode) == 1:
                        char = event.unicode
                        threading.Thread(
                            target=self.on_key, args=(f'CHAR:{char}',), daemon=True
                        ).start()
            clock.tick(60)

    # ── Internal draw helpers ─────────────────────────────────────────

    def _font(self, text_size, bold=False):
        return _get_font(text_size * 8, bold)

    def _char_w(self, text_size):
        return text_size * 6

    def _text(self, text, x, y, text_size, color=BLACK, bold=False):
        font = self._font(text_size, bold)
        img  = font.render(str(text), True, color)
        self._surface.blit(img, (x, y))

    def _text_centered(self, text, y, text_size, color=BLACK, clock=False, bold=False):
        font = _get_font(text_size * 8, bold=(bold or clock), clock=clock)
        w    = font.size(str(text))[0]
        x    = (self.WIDTH - w) // 2
        self._surface.blit(font.render(str(text), True, color), (x, y))

    def _line(self, y, weight=1):
        pygame.draw.line(self._surface, BLACK, (0, y), (self.WIDTH, y), weight)

    def _wrap_lines(self, text, text_size, max_px):
        char_w   = self._char_w(text_size)
        max_chars = max_px // char_w
        words     = text.split(' ')
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

        if prefix == 'LOCK':
            self._draw_lock(rest)
        elif prefix == 'HOME2':
            self._draw_home2(rest)
        elif prefix == 'TEXTS':
            self._draw_texts(rest)
        elif prefix == 'THREAD2':
            self._draw_thread2(rest)
        elif prefix == 'COMPOSE':
            self._draw_compose(rest)
        elif prefix == 'STUB':
            self._draw_stub(rest)
        # OS 0.0 legacy screens (kept for kyphone_app.py compatibility)
        elif prefix in ('HOME', 'HOME_FAST'):
            self._draw_home(rest)
        elif prefix in ('MSG_LIST', 'MSG_LIST_FAST'):
            self._draw_msg_list(rest)
        elif prefix == 'MSG_THREAD':
            self._draw_msg_thread(rest)
        else:
            self._draw_sms(command)

        pygame.display.flip()

    # ── OS 0.1 Screen Renderers ───────────────────────────────────────

    def _draw_lock(self, data):
        # data = "time_str|date_str|quote|attribution"
        parts = data.split('|')
        time_str = parts[0] if len(parts) > 0 else ''
        date_str = parts[1] if len(parts) > 1 else ''
        quote    = parts[2] if len(parts) > 2 else ''
        attr     = parts[3] if len(parts) > 3 else ''

        # OS 0.1 label — top left
        self._text('OS 0.1', 10, 8, 1)

        # ASCII cat — top right
        cat = [
            r"   )\._.,--....,'``.",
            r"  /,   _.. \   _\  (`._ ,.",
            r" `._.-(,_..'--(,_..'`-.;.'",
        ]
        cat_font = pygame.font.SysFont('courier', 12, bold=True)
        lh       = 14
        max_w    = max(cat_font.size(l)[0] for l in cat)
        cat_x    = self.WIDTH - max_w - 8
        for i, line in enumerate(cat):
            img = cat_font.render(line, True, BLACK)
            self._surface.blit(img, (cat_x, 6 + i * lh))

        # Clock — big, centered, vertically in the top half
        self._text_centered(time_str, 130, 8, clock=True)

        # Date — below clock
        self._text_centered(date_str, 210, 2)

        # Separator rule between clock area and quote area
        # (subtle, matches design's use of empty space)

        # Quote — centered, wrapped, bottom third
        quote_y  = 330
        margin   = 60
        max_px   = self.WIDTH - margin * 2
        lines    = self._wrap_lines(quote, 2, max_px)
        line_h   = 20
        for line in lines:
            w    = len(line) * self._char_w(2)
            x    = (self.WIDTH - w) // 2
            self._text(line, x, quote_y, 2)
            quote_y += line_h

        # Attribution — below quote
        if attr:
            w = len(attr) * self._char_w(1)
            x = (self.WIDTH - w) // 2
            self._text(attr, x, quote_y + 10, 1)

    def _draw_home2(self, data):
        # data = "time_str|home_index|unread"
        parts = data.split('|')
        time_str   = parts[0] if len(parts) > 0 else ''
        try:
            home_index = int(parts[1]) if len(parts) > 1 else 0
        except ValueError:
            home_index = 0
        try:
            unread = int(parts[2]) if len(parts) > 2 else 0
        except ValueError:
            unread = 0

        header_h = 60

        # Header: KYPHONE (left) + clock (right)
        self._text('KYPHONE', 24, 18, 2, bold=True)
        time_w = len(time_str) * self._char_w(3)
        self._text(time_str, self.WIDTH - 24 - time_w, 14, 3)
        self._line(header_h, weight=2)

        # 4 menu rows
        labels   = ['TEXT', 'CALL', 'READ', 'LISTEN']
        numbers  = ['01', '02', '03', '04']
        row_h    = 80
        pad_left = 64
        num_w    = 2 * self._char_w(2)   # "01" at textSize 2
        gap      = 20

        for i, (label, num) in enumerate(zip(labels, numbers)):
            y       = header_h + i * row_h
            sel     = i == home_index
            fg      = WHITE if sel else BLACK

            if sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))

            # Row number: small, slightly dimmed (we can't do real opacity, just smaller)
            text_y = y + (row_h - 4*8) // 2  # center textSize 4 (32px) vertically
            self._text(num, pad_left, text_y + 8, 2, fg)

            # App label: large, bold
            self._text(label, pad_left + num_w + gap, text_y, 4, fg, bold=True)
            self._text(label, pad_left + num_w + gap + 1, text_y, 4, fg, bold=True)

            # Unread badge on TEXT row
            if i == 0 and unread > 0:
                badge = str(min(unread, 9))
                badge_x = self.WIDTH - 50
                badge_y = y + (row_h - 24) // 2
                badge_col = WHITE if sel else BLACK
                txt_col   = BLACK if sel else WHITE
                pygame.draw.rect(self._surface, badge_col, (badge_x, badge_y, 28, 24))
                self._text(badge, badge_x + 6, badge_y + 4, 2, txt_col)

            self._line(y + row_h, weight=1)

        # Footer
        footer_y = 550
        self._line(footer_y, weight=2)
        self._text('BATT 82%', 28, footer_y + 14, 1)
        signal = '●●●○'  # ●●●○
        sig_w  = len(signal) * self._char_w(2)
        self._text(signal, self.WIDTH - 28 - sig_w, footer_y + 10, 2)

    def _draw_texts(self, data):
        # data = "idx|name·preview·unread|..."
        # idx: -1=back, -2=plus, >=0=row
        parts = data.split('|')
        try:
            idx     = int(parts[0])
            entries = parts[1:]
        except (ValueError, IndexError):
            idx     = 0
            entries = parts

        header_h = 44
        self._line(header_h - 1)

        def _header_btn(char, x, active=False):
            cw = self._char_w(3)
            ch = 3 * 8
            if active:
                pygame.draw.rect(self._surface, BLACK, (x - 4, 6, cw + 8, ch + 8))
                self._text(char, x, 10, 3, WHITE)
            else:
                self._text(char, x, 10, 3, BLACK)

        _header_btn('<', 16, active=(idx == -1))
        title   = 'TEXT'
        title_w = len(title) * self._char_w(3)
        self._text(title, (self.WIDTH - title_w) // 2, 10, 3, bold=True)
        _header_btn('+', self.WIDTH - 16 - self._char_w(3), active=(idx == -2))

        row_h  = 88
        margin = 28
        y      = header_h

        for i, entry in enumerate(entries):
            if y + row_h > self.HEIGHT:
                break
            fields  = entry.split('\xb7')
            name    = fields[0] if len(fields) > 0 else ''
            preview = fields[1] if len(fields) > 1 else ''
            unread  = fields[2] == '1' if len(fields) > 2 else False

            sel = i == idx
            fg  = WHITE if sel else BLACK
            if sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))

            # Name (bold if unread)
            self._text(name, margin, y + 10, 3, fg, bold=unread)
            if unread:
                self._text(name, margin + 1, y + 10, 3, fg, bold=True)

            # Chevron
            chevron_x = self.WIDTH - margin - self._char_w(2)
            self._text('>', chevron_x, y + (row_h - 16) // 2, 2, fg)

            # Preview
            preview_str = preview[:44]
            self._text(preview_str, margin, y + 52, 2, fg)

            self._line(y + row_h - 1)
            y += row_h

    def _draw_thread2(self, data):
        # data = "name|draft|Y:body|R:body|..."
        parts = data.split('|')
        name  = parts[0] if parts else ''
        draft = parts[1] if len(parts) > 1 else ''
        msgs  = parts[2:] if len(parts) > 2 else []

        # Header
        self._text('<', 16, 10, 3)
        self._text_centered(name, 10, 3, bold=True)
        self._text('i', self.WIDTH - 16 - self._char_w(3), 10, 3)
        self._line(46)

        # Reply bar pinned at bottom
        reply_y   = 555
        self._line(reply_y, weight=2)
        prompt    = '> '
        prompt_w  = len(prompt) * self._char_w(3)
        reply_x   = 24
        self._text(prompt, reply_x, reply_y + 9, 3)
        draft_x   = reply_x + prompt_w
        self._text(draft, draft_x, reply_y + 9, 3)
        # Cursor block
        cursor_x  = draft_x + len(draft) * self._char_w(3)
        pygame.draw.rect(self._surface, BLACK, (cursor_x, reply_y + 9, self._char_w(3), 24))

        # Messages (AIM style)
        y        = 56
        ts       = 3
        line_h   = 32
        margin   = 16
        max_w    = self.WIDTH - margin
        last_time = None

        for msg in msgs:
            if y >= reply_y - line_h:
                break

            if len(msg) >= 2 and msg[1] == ':':
                align = msg[0]
                rest  = msg[2:]
            else:
                align, rest = 'R', msg

            if '~' in rest:
                time_str, body = rest.split('~', 1)
            else:
                time_str, body = '', rest

            if time_str and time_str != last_time:
                last_time = time_str
                self._text_centered(time_str, y, 1)
                y += ts * 8 + 10

            sender_label = 'Me' if align == 'Y' else name
            prefix       = f"{sender_label}:"
            font_bold    = _get_font(ts * 8, bold=True)
            prefix_w     = font_bold.size(prefix + ' ')[0]
            wrap_w       = max_w - prefix_w

            lines = self._wrap_lines(body, ts, wrap_w)
            for i, line in enumerate(lines):
                if y >= reply_y - line_h:
                    break
                if i == 0:
                    self._surface.blit(font_bold.render(prefix, True, BLACK), (margin, y))
                    self._text(line, margin + prefix_w, y, ts)
                else:
                    self._text(line, margin + prefix_w, y, ts)
                y += line_h
            y += 4

    def _draw_compose(self, data):
        # data = "compose_to|compose_msg|to_active"
        parts     = data.split('|')
        to_str    = parts[0] if len(parts) > 0 else ''
        msg_str   = parts[1] if len(parts) > 1 else ''
        to_active = parts[2] != '0' if len(parts) > 2 else True

        # Header
        self._text('NEW MESSAGE', 24, 10, 3, bold=True)
        self._line(44, weight=2)

        # TO: field
        self._text('TO:', 24, 58, 1)
        self._text(to_str, 24, 72, 3)
        if to_active:
            cursor_x = 24 + len(to_str) * self._char_w(3)
            pygame.draw.rect(self._surface, BLACK, (cursor_x, 72, self._char_w(3), 24))
        self._line(100)

        # MESSAGE: field
        self._text('MESSAGE:', 24, 114, 1)
        # Wrap message text
        lines  = self._wrap_lines(msg_str, 2, self.WIDTH - 48) if msg_str else ['']
        msg_y  = 128
        line_h = 20
        for i, line in enumerate(lines):
            self._text(line, 24, msg_y + i * line_h, 2)
        if not to_active:
            last_line = lines[-1] if lines else ''
            cursor_x  = 24 + len(last_line) * self._char_w(2)
            cursor_y  = msg_y + (len(lines) - 1) * line_h
            pygame.draw.rect(self._surface, BLACK, (cursor_x, cursor_y, self._char_w(2), 16))

    def _draw_stub(self, data):
        # data = "app_name"
        name = data.strip()
        self._text(name, 24, 10, 3, bold=True)
        self._line(44, weight=2)
        msg = f"{name} — COMING SOON"
        self._text_centered(msg, 280, 2)

    # ── OS 0.0 Legacy Renderers (kept for kyphone_app.py) ────────────

    def _draw_home(self, data):
        parts = data.split('|')
        time_str = parts[0] if len(parts) > 0 else ''
        date_str = parts[1] if len(parts) > 1 else ''
        try:
            unread = int(parts[2]) if len(parts) > 2 else 0
        except ValueError:
            unread = 0
        try:
            home_sel = int(parts[3]) if len(parts) > 3 else -1
        except ValueError:
            home_sel = -1

        cat = [
            r"   )\._.,--....,'``.",
            r"  /,   _.. \   _\  (`._ ,.",
            r" `._.-(,_..'--(,_..'`-.;.'",
        ]
        cat_font = pygame.font.SysFont('courier', 13, bold=True)
        lh       = 13
        max_w    = max(cat_font.size(l)[0] for l in cat)
        cat_x    = self.WIDTH - max_w - 6
        for i, line in enumerate(cat):
            img = cat_font.render(line, True, BLACK)
            self._surface.blit(img, (cat_x, 6 + i * lh))

        total_h = 64 + 24 + 24
        start_y = 35 + (475 - total_h) // 2

        self._text_centered(time_str, start_y, 8, clock=True)
        self._text_centered(date_str, start_y + 64 + 24, 3)

        buttons      = ['Texts', 'Calls', 'Books', 'Music']
        btn_positions = [0, 150, 300, 450]
        btn_w, btn_h, btn_y = 150, 65, 535
        for i, (label, bx) in enumerate(zip(buttons, btn_positions)):
            cw       = len(label) * self._char_w(2)
            lx       = bx + (btn_w - cw) // 2
            ly       = btn_y + (btn_h - 16) // 2
            selected = i == home_sel and home_sel >= 0
            if selected:
                pygame.draw.rect(self._surface, BLACK, (bx, btn_y, btn_w, btn_h))
                self._text(label, lx, ly, 2, WHITE)
                self._text(label, lx + 1, ly, 2, WHITE)
            else:
                pygame.draw.rect(self._surface, BLACK, (bx, btn_y, btn_w, btn_h), 2)
                self._text(label, lx, ly, 2, BLACK)
                self._text(label, lx + 1, ly, 2, BLACK)

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
        try:
            sel     = int(parts[0])
            entries = parts[1:]
        except (ValueError, IndexError):
            sel     = 0
            entries = parts

        header_h = 44
        self._line(header_h - 1)

        def _header_btn(char, x, active=False):
            cw = self._char_w(3)
            ch = 3 * 8
            if active:
                pygame.draw.rect(self._surface, BLACK, (x - 4, 6, cw + 8, ch + 8))
                self._text(char, x, 10, 3, WHITE)
            else:
                self._text(char, x, 10, 3, BLACK)

        _header_btn('<', 16, active=(sel == -1))
        title_w = len('TEXTS') * self._char_w(3)
        self._text('TEXTS', (self.WIDTH - title_w) // 2, 10, 3)
        _header_btn('+', self.WIDTH - 16 - self._char_w(3), active=(sel == -2))

        row_h  = 72
        margin = 16
        y      = header_h
        for i, entry in enumerate(entries):
            fields  = entry.split('\xb7')
            name    = fields[0] if len(fields) > 0 else ''
            preview = fields[1] if len(fields) > 1 else ''
            ts      = fields[2] if len(fields) > 2 else ''

            fg = WHITE if i == sel else BLACK
            if i == sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))

            self._text(name, margin, y + 8, 3, fg, bold=True)

            chevron_w = self._char_w(2)
            chevron_x = self.WIDTH - margin - chevron_w
            chevron_y = y + (row_h - 16) // 2
            self._text('>', chevron_x, chevron_y, 2, fg)

            if ts:
                ts_w = len(ts) * self._char_w(2)
                self._text(ts, chevron_x - ts_w - 8, y + 16, 2, fg)

            self._text(preview, margin, y + 40, 2, fg)
            self._line(y + row_h - 1)
            y += row_h

    def _draw_msg_thread(self, data):
        parts = data.split('|')
        name  = parts[0] if parts else ''
        msgs  = parts[1:] if len(parts) > 1 else []

        self._text('<', 16, 10, 3)
        self._text_centered(name, 10, 3, bold=True)
        self._text('i', self.WIDTH - 16 - self._char_w(3), 10, 3)
        self._line(46)

        y        = 56
        ts       = 3
        line_h   = 32
        margin   = 16
        max_w    = self.WIDTH - margin
        last_time = None

        for msg in msgs:
            if len(msg) >= 2 and msg[1] == ':':
                align = msg[0]
                rest  = msg[2:]
            else:
                align, rest = 'R', msg

            if '~' in rest:
                time_str, body = rest.split('~', 1)
            else:
                time_str, body = '', rest

            if time_str and time_str != last_time:
                last_time = time_str
                self._text_centered(time_str, y, 1)
                y += ts * 8 + 10

            sender_label = 'Me' if align == 'Y' else name
            prefix       = f"{sender_label}:"
            font_bold    = _get_font(ts * 8, bold=True)
            prefix_w     = font_bold.size(prefix + ' ')[0]
            wrap_w       = max_w - prefix_w

            lines = self._wrap_lines(body, ts, wrap_w)
            for i, line in enumerate(lines):
                if i == 0:
                    self._surface.blit(font_bold.render(prefix, True, BLACK), (margin, y))
                    self._text(line, margin + prefix_w, y, ts)
                else:
                    self._text(line, margin + prefix_w, y, ts)
                y += line_h
            y += 4

    def _draw_sms(self, text):
        if '|' in text:
            sender, body = text.split('|', 1)
        else:
            sender, body = text, ''
        self._text(sender, 10, 10, 3)
        self._text(body,   10, 60, 4)
