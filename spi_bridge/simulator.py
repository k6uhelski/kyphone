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
        elif prefix == 'CONFIRMDISCARD':
            self._draw_confirm_discard(rest)
        elif prefix == 'CONTACTSPICK':
            self._draw_contacts_pick(rest)
        elif prefix == 'CONTACT':
            self._draw_contact(rest)
        elif prefix == 'CONTACTEDIT':
            self._draw_contact_edit(rest)
        elif prefix == 'CALLS':
            self._draw_calls(rest)
        elif prefix == 'DIAL':
            self._draw_dial(rest)
        elif prefix == 'CALLSTATE':
            self._draw_call_state(rest)
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

        # OS version — bottom left, textSize 2 (18px design token)
        self._text('OS 0.2', 10, self.HEIGHT - 8 - 2 * 8, 2)

        # ASCII cat — bottom right (fixed bitmap on device; unchanged since 0.1)
        cat = [
            r"   )\._.,--....,'``.",
            r"  /,   _.. \   _\  (`._ ,.",
            r" `._.-(,_..'--(,_..'`-.;.'",
        ]
        cat_font = pygame.font.SysFont('courier', 12, bold=True)
        lh       = 18
        max_w    = max(cat_font.size(l)[0] for l in cat)
        cat_x    = self.WIDTH - max_w - 8
        cat_y    = self.HEIGHT - (lh * len(cat)) - 6  # bottom margin, computed from content height
        for i, line in enumerate(cat):
            img = cat_font.render(line, True, BLACK)
            self._surface.blit(img, (cat_x, cat_y + i * lh))

        # Clock — textSize 8, centered, top:159
        self._text_centered(time_str, 159, 8, clock=True)

        # Date — textSize 3, centered, top:243
        self._text_centered(date_str, 243, 3)

        # Quote — textSize 3, centered, wrapped, top:356, line-height 34
        quote_y  = 356
        margin   = 60
        max_px   = self.WIDTH - margin * 2
        lines    = self._wrap_lines(quote, 3, max_px)
        line_h   = 34
        for line in lines:
            w    = len(line) * self._char_w(3)
            x    = (self.WIDTH - w) // 2
            self._text(line, x, quote_y, 3)
            quote_y += line_h

        # Attribution — textSize 2, centered, 12px below the quote block
        if attr:
            w = len(attr) * self._char_w(2)
            x = (self.WIDTH - w) // 2
            self._text(attr, x, quote_y + 12, 2)

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

        header_h   = 60
        header_sel = home_index == -1

        # Header: clock (left), battery + signal (right) — no KYPHONE label in 0.2
        if header_sel:
            pygame.draw.rect(self._surface, BLACK, (0, 0, self.WIDTH, header_h))
        header_fg = WHITE if header_sel else BLACK
        header_bg = BLACK if header_sel else WHITE
        self._text(time_str, 24, 18, 3, header_fg)
        self._draw_status_group(header_fg, header_bg, mid_y=header_h // 2)
        self._line(header_h, weight=2)

        # 5-row menu (TEXT/CALL/READ/LISTEN/CONTACTS), scrolling: 4 fill the
        # panel, the 5th scrolls into view when selected.
        row_h     = 135
        view_top  = header_h + 2
        view_h    = self.HEIGHT - view_top
        n         = len(self.HOME_MENU)
        shift     = max(0, (max(0, home_index) + 1) * row_h - view_h)

        prev_clip = self._surface.get_clip()
        self._surface.set_clip(pygame.Rect(0, view_top, self.WIDTH, view_h))
        for i, label in enumerate(self.HOME_MENU):
            y   = view_top + i * row_h - shift
            if y + row_h < view_top or y > self.HEIGHT:
                continue
            sel = i == home_index
            fg  = WHITE if sel else BLACK
            if sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))

            # Label — textSize 6, bold, centered (icons are a separate task)
            label_font = _get_font(6 * 8, bold=True)
            label_w    = label_font.size(label)[0]
            label_x    = (self.WIDTH - label_w) // 2
            label_y    = y + (row_h - 6 * 8) // 2
            self._text(label, label_x, label_y, 6, fg, bold=True)

            # Unread count hangs to the right of the TEXT row's content
            if label == 'TEXT' and unread > 0:
                count_x = label_x + label_w + 24
                self._text(str(unread), count_x, label_y + (6 * 8 - 3 * 8) // 2, 3, fg)

            self._line(y + row_h, weight=1)
        self._surface.set_clip(prev_clip)

        # "More below" chevron — three shrinking bars, bottom right
        if n > 4 and home_index <= 3:
            cx = self.WIDTH - 12
            cy = self.HEIGHT - 6
            for w in (14, 8, 3):
                pygame.draw.rect(self._surface, BLACK, (cx - w, cy - 3, w, 3))
                cy -= 5

    HOME_MENU = ['TEXT', 'CALL', 'READ', 'LISTEN', 'CONTACTS']

    def _draw_status_group(self, fg, bg, mid_y):
        """Battery block + percentage + 4-bar signal staircase, right-aligned
        in the home header. No real telemetry exists yet — fixed placeholder
        values, swappable for real readings later."""
        batt_pct = 82
        pct_str  = f'{batt_pct}%'
        pct_w    = len(pct_str) * self._char_w(3)
        sig_heights = [5, 9, 13, 17]
        sig_w    = 4
        sig_gap  = 3
        sig_group_w = sig_w * 4 + sig_gap * 3

        batt_w, batt_h, batt_border = 34, 18, 2
        nub_w, nub_h = 3, 8

        total_w = batt_w + 2 + nub_w + 14 + pct_w + 14 + sig_group_w
        x = self.WIDTH - 24 - total_w

        # Battery block
        by = mid_y - batt_h // 2
        pygame.draw.rect(self._surface, fg, (x, by, batt_w, batt_h), batt_border)
        fill_w = int((batt_w - 2 * batt_border) * (batt_pct / 100))
        pygame.draw.rect(self._surface, fg, (x + batt_border, by + batt_border, fill_w, batt_h - 2 * batt_border))
        x += batt_w + 2
        pygame.draw.rect(self._surface, fg, (x, mid_y - nub_h // 2, nub_w, nub_h))
        x += nub_w + 14

        # Percentage
        self._text(pct_str, x, mid_y - 3 * 8 // 2, 3, fg)
        x += pct_w + 14

        # Signal staircase — last bar is an outline only
        sig_bottom = mid_y + sig_heights[-1] // 2
        for i, h in enumerate(sig_heights):
            rect = (x, sig_bottom - h, sig_w, h)
            if i == len(sig_heights) - 1:
                pygame.draw.rect(self._surface, fg, rect, 1)
            else:
                pygame.draw.rect(self._surface, fg, rect)
            x += sig_w + sig_gap

    def _draw_header_bar(self, title, back_active, plus_active, height=44, rule_weight=1):
        """Shared back/title/+ header used by list screens — each control is
        a literal 38x34 hit box that inverts when selected."""
        self._line(height - 1, weight=rule_weight)
        box_w, box_h = 38, 34

        def _btn(char, x, active):
            if active:
                pygame.draw.rect(self._surface, BLACK, (x, 6, box_w, box_h))
                fg = WHITE
            else:
                fg = BLACK
            cw = self._char_w(3)
            self._text(char, x + (box_w - cw) // 2, 6 + (box_h - 3 * 8) // 2, 3, fg, bold=True)

        _btn('<', 16, back_active)
        title_w = len(title) * self._char_w(3)
        self._text(title, (self.WIDTH - title_w) // 2, 10, 3, bold=True)
        _btn('+', self.WIDTH - 16 - box_w, plus_active)

    def _draw_texts(self, data):
        # data = "idx|name·preview·unread·time|..."
        # idx: -1=back, -2=plus, >=0=row
        parts = data.split('|')
        try:
            idx     = int(parts[0])
            entries = parts[1:]
        except (ValueError, IndexError):
            idx     = 0
            entries = parts

        self._draw_header_bar('TEXT', idx == -1, idx == -2)

        row_h  = 88
        margin = 28
        y      = 44

        for i, entry in enumerate(entries):
            if y + row_h > self.HEIGHT:
                break
            fields  = entry.split('\xb7')
            name    = fields[0] if len(fields) > 0 else ''
            preview = fields[1] if len(fields) > 1 else ''
            unread  = fields[2] == '1' if len(fields) > 2 else False
            time_str = fields[3] if len(fields) > 3 else ''

            sel = i == idx
            fg  = WHITE if sel else BLACK
            if sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))

            # Name (left) — bold if unread
            self._text(name, margin, y + 10, 3, fg, bold=unread)
            if unread:
                self._text(name, margin + 1, y + 10, 3, fg, bold=True)

            # Time + chevron (right), baseline-aligned with the name
            chevron_x = self.WIDTH - margin - self._char_w(2)
            self._text('>', chevron_x, y + 14, 2, fg)
            if time_str:
                time_w = len(time_str) * self._char_w(2)
                self._text(time_str, chevron_x - time_w - 10, y + 14, 2, fg)

            # Preview
            preview_str = preview[:44]
            self._text(preview_str, margin, y + 48, 2, fg)

            self._line(y + row_h - 1)
            y += row_h

    def _draw_thread2(self, data):
        # data = "name|draft|hdr|Y:time~body|R:time~body|..."  hdr: ''=typing 'B'=back 'I'=info
        parts = data.split('|')
        name  = parts[0] if parts else ''
        draft = parts[1] if len(parts) > 1 else ''
        hdr   = parts[2] if len(parts) > 2 else ''
        msgs  = parts[3:] if len(parts) > 3 else []

        box_w, box_h = 38, 34
        back_sel = hdr == 'B'
        info_sel = hdr == 'I'

        if back_sel:
            pygame.draw.rect(self._surface, BLACK, (16, 6, box_w, box_h))
        self._text('<', 16 + (box_w - self._char_w(3)) // 2, 6 + (box_h - 24) // 2, 3,
                    WHITE if back_sel else BLACK, bold=True)
        self._text_centered(name, 10, 3, bold=True)
        info_x = self.WIDTH - 16 - box_w
        if info_sel:
            pygame.draw.rect(self._surface, BLACK, (info_x, 6, box_w, box_h))
        self._text('i', info_x + (box_w - self._char_w(3)) // 2, 6 + (box_h - 24) // 2, 3,
                    WHITE if info_sel else BLACK, bold=True)
        self._line(46, weight=2)

        # Composer — pinned to the bottom 45px
        composer_h = 45
        reply_y    = self.HEIGHT - composer_h
        self._line(reply_y, weight=2)
        prompt   = '> '
        field_y  = reply_y + (composer_h - 24) // 2
        self._text(prompt, 24, field_y, 3)
        draft_x  = 24 + len(prompt) * self._char_w(3)
        self._text(draft, draft_x, field_y, 3)
        cursor_x = draft_x + len(draft) * self._char_w(3)
        pygame.draw.rect(self._surface, BLACK, (cursor_x, field_y, self._char_w(3), 24))

        # Messages — 2px-bordered bubbles stacked from the bottom. Incoming
        # left/paper with a stepped pixel tail and the sender's name above
        # each incoming run; outgoing right/filled ink. Time sits under
        # every bubble.
        parsed = []
        for m in msgs:
            if len(m) >= 2 and m[1] == ':':
                align, rest = m[0], m[2:]
            else:
                align, rest = 'R', m
            if '~' in rest:
                time_str, body = rest.split('~', 1)
            else:
                time_str, body = '', rest
            parsed.append((align, time_str, body))

        pad_x, pad_y   = 12, 8
        bubble_line_h  = 35   # 24px text at line-height 1.45
        max_bubble_w   = 400
        inner_w        = max_bubble_w - 2 * pad_x
        margin         = 16
        tail_widths    = [4, 8, 14, 8, 4]
        tail_seg_h     = 4
        tail_span      = max(tail_widths) + 2
        gap            = 16
        top, bottom    = 62, self.HEIGHT - 62

        blocks, prev_align = [], None
        for align, time_str, body in parsed:
            lines     = self._wrap_lines(body, 3, inner_w)
            show_name = align != 'Y' and prev_align != align
            bubble_h  = pad_y * 2 + len(lines) * bubble_line_h
            h = bubble_h + 8 * 2 + 4
            if show_name:
                h += 8 * 2 + 6
            blocks.append({'align': align, 'time': time_str, 'lines': lines,
                            'show_name': show_name, 'bubble_h': bubble_h, 'h': h})
            prev_align = align

        # Keep the newest blocks that fit the message region, bottom-up.
        fitted, used = [], 0
        for b in reversed(blocks):
            extra = b['h'] + (gap if fitted else 0)
            if used + extra > bottom - top:
                break
            used += extra
            fitted.insert(0, b)

        y = bottom - used
        for b in fitted:
            block_top = y
            y0 = y
            if b['show_name']:
                self._text(name.upper(), margin + tail_span, y0, 2, bold=True)
                y0 += 8 * 2 + 6

            line_w = max((len(l) for l in b['lines']), default=0) * self._char_w(3)
            bw = min(max_bubble_w, line_w + 2 * pad_x)
            outgoing = b['align'] == 'Y'
            bx = (self.WIDTH - margin - bw) if outgoing else (margin + tail_span)

            fill    = BLACK if outgoing else WHITE
            text_fg = WHITE if outgoing else BLACK
            pygame.draw.rect(self._surface, fill, (bx, y0, bw, b['bubble_h']))
            pygame.draw.rect(self._surface, BLACK, (bx, y0, bw, b['bubble_h']), 2)
            ly = y0 + pad_y
            for line in b['lines']:
                self._text(line, bx + pad_x, ly, 3, text_fg)
                ly += bubble_line_h

            tail_total_h = tail_seg_h * len(tail_widths)
            tail_y       = y0 + (b['bubble_h'] - tail_total_h) // 2
            for i, w in enumerate(tail_widths):
                tx = (bx + bw + 2) if outgoing else (bx - 2 - w)
                pygame.draw.rect(self._surface, BLACK, (tx, tail_y + i * tail_seg_h, w, tail_seg_h))

            time_y = y0 + b['bubble_h'] + 6
            time_w = len(b['time']) * self._char_w(2)
            time_x = (bx + bw - time_w) if outgoing else bx
            self._text(b['time'], time_x, time_y, 2)

            y = block_top + b['h'] + gap

    def _draw_field_label(self, text, x, y, active):
        """A field label that inverts (fills ink, text flips to paper) while
        its field is active — the mode is marked at the field, not just by
        cursor position."""
        w = len(text) * self._char_w(2) + 4
        h = 8 * 2 + 4
        if active:
            pygame.draw.rect(self._surface, BLACK, (x - 2, y - 2, w, h))
        self._text(text, x, y, 2, WHITE if active else BLACK)

    def _draw_compose(self, data):
        # data = "to|msg|to_active|hdr|plus_sel|send_sel"  hdr: ''=typing 'X'=exit
        parts     = data.split('|')
        to_str    = parts[0] if len(parts) > 0 else ''
        msg_str   = parts[1] if len(parts) > 1 else ''
        to_active = parts[2] != '0' if len(parts) > 2 else True
        x_sel     = parts[3] == 'X' if len(parts) > 3 else False
        plus_sel  = parts[4] == '1' if len(parts) > 4 else False
        send_sel  = parts[5] == '1' if len(parts) > 5 else False

        box_w, box_h = 38, 34

        # Header
        self._text('NEW MESSAGE', 24, 10, 3, bold=True)
        self._text('NEW MESSAGE', 25, 10, 3, bold=True)
        x_box = (self.WIDTH - 16 - box_w, 8)
        if x_sel:
            pygame.draw.rect(self._surface, BLACK, (*x_box, box_w, box_h))
        self._text('X', x_box[0] + (box_w - self._char_w(3)) // 2, x_box[1] + (box_h - 24) // 2, 3,
                    WHITE if x_sel else BLACK, bold=True)
        self._line(44, weight=2)

        # TO: field — label inverts while active; empty+active shows a '+'
        # hint on the right that opens the contact picker
        self._draw_field_label('TO:', 24, 58, to_active)
        self._text(to_str, 24, 84, 3)
        if to_active and not to_str:
            plus_box = (self.WIDTH - 24 - box_w, 79)
            if plus_sel:
                pygame.draw.rect(self._surface, BLACK, (*plus_box, box_w, box_h))
            self._text('+', plus_box[0] + (box_w - self._char_w(3)) // 2, plus_box[1] + (box_h - 24) // 2,
                        3, WHITE if plus_sel else BLACK, bold=True)
        elif to_active and not plus_sel:
            cursor_x = 24 + len(to_str) * self._char_w(3)
            pygame.draw.rect(self._surface, BLACK, (cursor_x, 84, self._char_w(3), 24))
        self._line(122)

        # MESSAGE: field — label inverts while active
        self._draw_field_label('MESSAGE:', 24, 134, not to_active)
        msg_active = not to_active and not send_sel
        lines  = self._wrap_lines(msg_str, 3, self.WIDTH - 48) if msg_str else ['']
        msg_y  = 162
        line_h = 34
        for line in lines:
            self._text(line, 24, msg_y, 3)
            msg_y += line_h
        if msg_active:
            last_line = lines[-1] if lines else ''
            cursor_x  = 24 + len(last_line) * self._char_w(3)
            cursor_y  = msg_y - line_h
            pygame.draw.rect(self._surface, BLACK, (cursor_x, cursor_y, 12, 16))

        # SEND — bottom right; activates the same as Enter on a filled-out message
        send_str = 'SEND'
        send_w   = len(send_str) * self._char_w(2) + 36
        send_x   = self.WIDTH - 24 - send_w
        send_y   = self.HEIGHT - 14 - (16 + 12)
        if send_sel:
            pygame.draw.rect(self._surface, BLACK, (send_x, send_y, send_w, 16 + 12))
        pygame.draw.rect(self._surface, BLACK, (send_x, send_y, send_w, 16 + 12), 3)
        self._text(send_str, send_x + 18, send_y + 6, 2, WHITE if send_sel else BLACK, bold=True)

    def _draw_alert_icon_body(self, top, body):
        """Boxed '!' + prose paragraph — the alert pattern used by stub
        screens and the discard confirmation: what happened, why, what to
        do instead, all in one paragraph."""
        icon_size = 46
        icon_x    = 56
        pygame.draw.rect(self._surface, BLACK, (icon_x, top, icon_size, icon_size), 2)
        self._text('!', icon_x + (icon_size - self._char_w(3)) // 2,
                    top + (icon_size - 24) // 2, 3, bold=True)
        text_x  = icon_x + icon_size + 22
        max_px  = self.WIDTH - 56 - text_x
        lines   = self._wrap_lines(body, 3, max_px)
        ly = top
        for line in lines:
            self._text(line, text_x, ly, 3)
            ly += 34

    def _draw_stub(self, data):
        # data = "title|body"
        parts = data.split('|', 1)
        title = parts[0] if len(parts) > 0 else ''
        body  = parts[1] if len(parts) > 1 else ''
        self._text(title, 24, 10, 3, bold=True)
        self._line(44, weight=2)
        self._draw_alert_icon_body(212, body)

        label = 'OK'
        w, h  = len(label) * self._char_w(2) + 36, 16 + 12
        x, y  = self.WIDTH - 24 - w, self.HEIGHT - 24 - h
        pygame.draw.rect(self._surface, BLACK, (x, y, w, h), 3)
        self._text(label, x + 18, y + 6, 2, bold=True)

    def _draw_confirm_discard(self, data):
        discard_sel = data.strip() == 'D'
        self._text('NEW MESSAGE', 24, 10, 3, bold=True)
        self._line(44, weight=2)
        body = ("DISCARD THIS MESSAGE? IT HAS NOT BEEN SENT, AND THE PHONE KEEPS "
                "NO DRAFTS, SO THE TEXT CANNOT BE BROUGHT BACK.")
        self._draw_alert_icon_body(196, body)

        d_label, d_h = 'DISCARD', 16 + 14
        d_w = len(d_label) * self._char_w(2) + 36
        d_x, d_y = 56, self.HEIGHT - 24 - d_h
        if discard_sel:
            pygame.draw.rect(self._surface, BLACK, (d_x, d_y, d_w, d_h))
        pygame.draw.rect(self._surface, BLACK, (d_x, d_y, d_w, d_h), 2)
        self._text(d_label, d_x + 18, d_y + 7, 2, WHITE if discard_sel else BLACK)

        k_label, k_h = 'KEEP EDITING', 16 + 12
        k_w = len(k_label) * self._char_w(2) + 36
        k_x, k_y = self.WIDTH - 24 - k_w, self.HEIGHT - 24 - k_h
        if not discard_sel:
            pygame.draw.rect(self._surface, BLACK, (k_x, k_y, k_w, k_h))
        pygame.draw.rect(self._surface, BLACK, (k_x, k_y, k_w, k_h), 3)
        self._text(k_label, k_x + 18, k_y + 6, 2, WHITE if not discard_sel else BLACK, bold=True)

    def _draw_contacts_pick(self, data):
        # data = "idx|query|name·number|..."  idx: -1=back, -2=plus, >=0=row
        parts = data.split('|')
        try:
            idx = int(parts[0])
        except (ValueError, IndexError):
            idx = 0
        query   = parts[1] if len(parts) > 1 else ''
        entries = parts[2:] if len(parts) > 2 else []

        self._draw_header_bar('CONTACTS', idx == -1, idx == -2)

        row_h, bar_h = 64, 45
        y = 44
        for i, entry in enumerate(entries):
            if y + row_h > self.HEIGHT - bar_h:
                break
            fields = entry.split('\xb7')
            name   = fields[0] if len(fields) > 0 else ''
            number = fields[1] if len(fields) > 1 else ''
            sel    = i == idx
            fg     = WHITE if sel else BLACK
            if sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))
            self._text(name, 28, y + (row_h - 24) // 2, 3, fg, bold=True)
            num_w = len(number) * self._char_w(2)
            self._text(number, self.WIDTH - 28 - num_w, y + (row_h - 16) // 2, 2, fg)
            self._line(y + row_h - 1)
            y += row_h

        bar_y = self.HEIGHT - bar_h
        self._line(bar_y, weight=2)
        label = 'LOOK UP:'
        self._text(label, 28, bar_y + (bar_h - 16) // 2, 2)
        qx = 28 + len(label) * self._char_w(2) + 14
        self._text(query, qx, bar_y + (bar_h - 24) // 2, 3)
        cursor_x = qx + len(query) * self._char_w(3)
        pygame.draw.rect(self._surface, BLACK, (cursor_x, bar_y + (bar_h - 24) // 2, self._char_w(3), 24))

    def _draw_contact(self, data):
        # data = "name|number|sel"  sel: B=back C=call T=text E=edit
        parts  = data.split('|')
        name   = parts[0] if len(parts) > 0 else ''
        number = parts[1] if len(parts) > 1 else 'NO NUMBER SAVED'
        sel    = parts[2] if len(parts) > 2 else 'C'
        box_w, box_h = 38, 34

        back_sel = sel == 'B'
        if back_sel:
            pygame.draw.rect(self._surface, BLACK, (16, 6, box_w, box_h))
        self._text('<', 16 + (box_w - self._char_w(3)) // 2, 6 + (box_h - 24) // 2, 3,
                    WHITE if back_sel else BLACK, bold=True)
        self._text_centered('CONTACT', 10, 3, bold=True)

        edit_sel = sel == 'E'
        edit_label = 'EDIT'
        edit_w, edit_h = len(edit_label) * self._char_w(2) + 20, 34
        edit_x = self.WIDTH - 16 - edit_w
        if edit_sel:
            pygame.draw.rect(self._surface, BLACK, (edit_x, 6, edit_w, edit_h))
        self._text(edit_label, edit_x + 10, 6 + (edit_h - 16) // 2, 2, WHITE if edit_sel else BLACK, bold=True)
        self._line(43)

        self._text(name, 28, 150, 6, bold=True)
        self._text(number, 28, 150 + 48 + 18, 3)
        self._line(330)

        call_sel, text_sel = sel == 'C', sel == 'T'
        btn_y, btn_h = 360, 24 + 16
        call_label = 'CALL'
        call_w = len(call_label) * self._char_w(3) + 44
        if call_sel:
            pygame.draw.rect(self._surface, BLACK, (28, btn_y, call_w, btn_h))
        pygame.draw.rect(self._surface, BLACK, (28, btn_y, call_w, btn_h), 3)
        self._text(call_label, 28 + 22, btn_y + 8, 3, WHITE if call_sel else BLACK, bold=True)

        text_x = 28 + call_w + 16
        text_label = 'TEXT'
        text_w = len(text_label) * self._char_w(3) + 44
        if text_sel:
            pygame.draw.rect(self._surface, BLACK, (text_x, btn_y, text_w, btn_h))
        pygame.draw.rect(self._surface, BLACK, (text_x, btn_y, text_w, btn_h), 3)
        self._text(text_label, text_x + 22, btn_y + 8, 3, WHITE if text_sel else BLACK, bold=True)

    def _draw_contact_edit(self, data):
        # data = "first|last|number|idx"  idx: -1=cancel, 0/1/2=field, 3=save
        parts = data.split('|')
        first  = parts[0] if len(parts) > 0 else ''
        last   = parts[1] if len(parts) > 1 else ''
        number = parts[2] if len(parts) > 2 else ''
        try:
            idx = int(parts[3]) if len(parts) > 3 else 0
        except ValueError:
            idx = 0
        box_w, box_h = 38, 34

        cancel_sel = idx == -1
        if cancel_sel:
            pygame.draw.rect(self._surface, BLACK, (16, 6, box_w, box_h))
        self._text('X', 16 + (box_w - self._char_w(3)) // 2, 6 + (box_h - 24) // 2, 3,
                    WHITE if cancel_sel else BLACK, bold=True)
        self._text_centered('EDIT CONTACT', 10, 3, bold=True)
        self._line(43)

        def _field(label, value, y_label, y_value, y_rule, active):
            self._draw_field_label(label, 24, y_label, active)
            self._text(value, 24, y_value, 3)
            if active:
                cx = 24 + len(value) * self._char_w(3)
                pygame.draw.rect(self._surface, BLACK, (cx, y_value, self._char_w(3), 24))
            self._line(y_rule)

        _field('FIRST NAME:', first, 70, 100, 138, idx == 0)
        _field('LAST NAME:', last, 158, 188, 226, idx == 1)
        _field('PHONE NUMBER:', number, 246, 276, 314, idx == 2)

        save_sel = idx == 3
        label = 'SAVE'
        w, h  = len(label) * self._char_w(2) + 36, 16 + 12
        x, y  = self.WIDTH - 24 - w, self.HEIGHT - 14 - h
        if save_sel:
            pygame.draw.rect(self._surface, BLACK, (x, y, w, h))
        pygame.draw.rect(self._surface, BLACK, (x, y, w, h), 3)
        self._text(label, x + 18, y + 6, 2, WHITE if save_sel else BLACK, bold=True)

    def _draw_calls(self, data):
        # data = "idx|name·tag·time·duration|..."  idx: -1=back, 0=DIAL A NUMBER, 1+=log
        parts = data.split('|')
        try:
            idx = int(parts[0])
        except (ValueError, IndexError):
            idx = 0
        entries = parts[1:] if len(parts) > 1 else []

        self._draw_header_bar_back_only('CALL', idx == -1)

        row_h = 76
        y = 44
        sel = idx == 0
        fg  = WHITE if sel else BLACK
        if sel:
            pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))
        self._text('DIAL A NUMBER', 28, y + (row_h - 24) // 2, 3, fg, bold=True)
        self._line(y + row_h - 1)
        y += row_h

        for i, entry in enumerate(entries):
            if y + row_h > self.HEIGHT:
                break
            fields = entry.split('\xb7')
            name = fields[0] if len(fields) > 0 else ''
            tag  = fields[1] if len(fields) > 1 else ''
            t    = fields[2] if len(fields) > 2 else ''
            dur  = fields[3] if len(fields) > 3 else ''
            sel  = (i + 1) == idx
            fg   = WHITE if sel else BLACK
            if sel:
                pygame.draw.rect(self._surface, BLACK, (0, y, self.WIDTH, row_h))
            self._text(name, 28, y + (row_h - 24) // 2, 3, fg, bold=True)
            tag_x = 28 + len(name) * self._char_w(3) + 12
            tag_w = len(tag) * self._char_w(2) + 12
            pygame.draw.rect(self._surface, fg, (tag_x, y + (row_h - 16) // 2, tag_w, 16), 1)
            self._text(tag, tag_x + 6, y + (row_h - 16) // 2, 2, fg)
            t_w = len(t) * self._char_w(2)
            self._text(t, self.WIDTH - 28 - t_w, y + (row_h - 38) // 2, 2, fg)
            dur_w = len(dur) * self._char_w(2)
            self._text(dur, self.WIDTH - 28 - dur_w, y + (row_h - 38) // 2 + 22, 2, fg)
            self._line(y + row_h - 1)
            y += row_h

    def _draw_header_bar_back_only(self, title, back_active, height=44):
        """Header with only a back control — used by CALL, whose right side
        carries no additive action."""
        self._line(height - 1)
        box_w, box_h = 38, 34
        if back_active:
            pygame.draw.rect(self._surface, BLACK, (16, 6, box_w, box_h))
        self._text('<', 16 + (box_w - self._char_w(3)) // 2, 6 + (box_h - 24) // 2, 3,
                    WHITE if back_active else BLACK, bold=True)
        title_w = len(title) * self._char_w(3)
        self._text(title, (self.WIDTH - title_w) // 2, 10, 3, bold=True)

    def _draw_dial(self, data):
        # data = "buffer|quick_idx|name|..."
        parts = data.split('|')
        buf  = parts[0] if len(parts) > 0 else ''
        try:
            qidx = int(parts[1]) if len(parts) > 1 else -1
        except ValueError:
            qidx = -1
        names = parts[2:] if len(parts) > 2 else []

        self._text_centered('DIAL', 40, 2)
        buf_w, cursor_w = len(buf) * self._char_w(6), 28
        x = (self.WIDTH - buf_w - cursor_w) // 2
        self._text(buf, x, 70, 6, bold=True)
        pygame.draw.rect(self._surface, BLACK, (x + buf_w, 70, cursor_w, 48))
        self._line(170, weight=2)
        self._text('RECENT', 28, 186, 2)

        row_h = 56
        y = 210
        for i, name in enumerate(names):
            sel = i == qidx
            fg  = WHITE if sel else BLACK
            if sel:
                pygame.draw.rect(self._surface, BLACK, (28, y, self.WIDTH - 56, row_h))
            self._text(name, 32, y + (row_h - 24) // 2, 3, fg, bold=True)
            pygame.draw.line(self._surface, BLACK, (28, y + row_h - 1), (self.WIDTH - 28, y + row_h - 1), 1)
            y += row_h

    def _draw_call_state(self, data):
        # data = "OUT|IN|ACTIVE|name|timer"
        parts = data.split('|')
        call_state = parts[0] if len(parts) > 0 else 'OUT'
        name  = parts[1] if len(parts) > 1 else ''
        timer = parts[2] if len(parts) > 2 else '00:00'

        if call_state == 'IN':
            self._surface.fill(BLACK)
            fg, label, hint = WHITE, 'INCOMING CALL', 'ENTER ACCEPT \xb7 ESC DECLINE'
        elif call_state == 'ACTIVE':
            fg, label, hint = BLACK, 'IN CALL', 'ESC HANG UP'
        else:
            fg, label, hint = BLACK, 'CALLING…', 'ESC HANG UP'

        self._text_centered(label, 220, 2, color=fg)
        self._text_centered(name, 280, 6, color=fg, bold=True)
        if call_state == 'ACTIVE':
            self._text_centered(timer, 360, 3, color=fg)
        self._text_centered(hint, self.HEIGHT - 34 - 16, 2, color=fg)

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
