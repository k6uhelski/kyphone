#include <Inkplate.h>
#include <driver/gpio.h>
#include "soc/io_mux_reg.h"

Inkplate display(INKPLATE_1BIT); 

// Shared pins
#define PIN_MOSI 13
#define PIN_SCLK 14
#define PIN_CS   15

// Handshake
#define PIN_HANDSHAKE IO_PIN_B0 // P1-0 expander pin

// --- ISR Variables ---
#define PAYLOAD_BYTES 256
#define TOTAL_BITS (PAYLOAD_BYTES * 8)

volatile uint8_t rx_buf[PAYLOAD_BYTES];
uint8_t page_transition_count = 0;  // full refresh every 10 page changes
char current_screen[32] = "BOOT";
volatile uint16_t bit_counter = 0;
volatile bool transfer_complete = false;
volatile uint32_t last_sclk_time = 0;
volatile uint32_t last_cs_time = 0;

// Debug Counters
volatile uint32_t debug_cs_falling = 0;
volatile uint32_t debug_sclk_total = 0;
volatile uint32_t first_sclk_time = 0;

// --- RECLAIM SPI PINS FROM INKPLATE LIBRARY ---
void reclaim_spi_pins_for_gpio() {
    // GPIO 15 (CS): Force out of SPI CS0 function, add pull-up vs strapping pull-down
    PIN_FUNC_SELECT(IO_MUX_GPIO15_REG, 2);
    gpio_set_pull_mode((gpio_num_t)PIN_CS, GPIO_PULLUP_ONLY);
    gpio_config_t cs_conf = {
        .pin_bit_mask = (1ULL << PIN_CS),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_conf);

    // GPIO 13 (MOSI): Force out of SPI MOSI function — idles LOW in SPI mode, masking data
    PIN_FUNC_SELECT(IO_MUX_GPIO13_REG, 2);
    gpio_config_t mosi_conf = {
        .pin_bit_mask = (1ULL << PIN_MOSI),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&mosi_conf);
}

// --- ISR: SPI Clock ---
void IRAM_ATTR spi_clock_isr() {
    uint32_t now = micros();
    debug_sclk_total++;
    if (bit_counter == 0) first_sclk_time = now;
    last_sclk_time = now;

    if (bit_counter == 0) {
        // First edge: signal busy immediately so Master won't re-send
        // Note: digitalWriteIO is not ISR-safe; handled in loop() via flag
        // This comment left intentionally — handshake pulled LOW in loop() on first bit
    }

    if (bit_counter < TOTAL_BITS) {
        bool mosi_high = (GPIO.in >> PIN_MOSI) & 0x1; // Faster read
        uint8_t byte_idx = bit_counter / 8;
        uint8_t bit_idx = 7 - (bit_counter % 8); 
        
        if (mosi_high) rx_buf[byte_idx] |= (1 << bit_idx);
        else           rx_buf[byte_idx] &= ~(1 << bit_idx);
        
        bit_counter++;
    }
}

// --- ISR: Chip Select (Falling) ---
void IRAM_ATTR cs_falling_isr() {
    uint32_t now = micros();
    // Debounce: Ignore edges within 1ms of each other
    if (now - last_cs_time > 1000) {
        debug_cs_falling++;
        bit_counter = 0;
        transfer_complete = false;
        // Don't zero the whole buffer in ISR if we are noisy
    }
    last_cs_time = now;
}

// --- Screen Renderers ---

void render_home(char* data) {
    // data = "HH:MM|Day, Mon DD|{unread}|{home_sel}"
    // home_sel: -1=none, 0=TEXT, 1=CALL, 2=READ, 3=LISTEN
    char time_str[16] = "";
    char date_str[32] = "";
    int unread = 0;
    int home_sel = -1;

    char* p1 = strchr(data, '|');
    if (p1 != NULL) {
        snprintf(time_str, sizeof(time_str), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2 != NULL) {
            snprintf(date_str, sizeof(date_str), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            char* p3 = strchr(p2 + 1, '|');
            if (p3 != NULL) {
                char unread_buf[8] = "";
                snprintf(unread_buf, sizeof(unread_buf), "%.*s", (int)(p3 - p2 - 1), p2 + 1);
                unread = atoi(unread_buf);
                home_sel = atoi(p3 + 1);
            } else {
                unread = atoi(p2 + 1);
            }
        } else {
            strncpy(date_str, p1 + 1, sizeof(date_str) - 1);
        }
    } else {
        strncpy(time_str, data, sizeof(time_str) - 1);
    }

    // ASCII art — top right, textSize 2 = 12px wide x 16px tall per char
    const char* cat[] = {
        "   )\\._.,--....,'``.",
        "  /,   _.. \\   _\\  (`._ ,.",
        " `._.-(,_..'--(,_..'`-.;.'",
    };
    display.setTextSize(2);
    for (int ci = 0; ci < 3; ci++) {
        int lw = strlen(cat[ci]) * 12;
        int cx = 600 - lw - 6, cy = 6 + ci * 18;
        display.setCursor(cx, cy);     display.print(cat[ci]);
        display.setCursor(cx + 1, cy); display.print(cat[ci]);
    }

    // Clock + date vertically centered above button row (y=35 to ~520)
    int total_h = 80 + 24 + 24;
    int start_y = 35 + (475 - total_h) / 2;

    // Clock — textSize 8 = 48px wide per char, 64px tall
    display.setTextSize(8);
    int clock_w = strlen(time_str) * 48;
    display.setCursor((600 - clock_w) / 2, start_y);
    display.print(time_str);

    // Date — textSize 3 = 18px wide per char, 24px tall
    display.setTextSize(3);
    int date_w = strlen(date_str) * 18;
    display.setCursor((600 - date_w) / 2, start_y + 80 + 24);
    display.print(date_str);

    // 4 buttons: Texts, Calls, Books, Music
    const char* buttons[] = {"Texts", "Calls", "Books", "Music"};
    int btn_positions[] = {0, 150, 300, 450};
    int btn_w = 150, btn_h = 65, btn_y = 535;

    for (int i = 0; i < 4; i++) {
        int bx = btn_positions[i];
        bool selected = (i == home_sel);
        int cw = strlen(buttons[i]) * 12; // textSize 2: 6*2=12px per char
        int lx = bx + (btn_w - cw) / 2;
        int ly = btn_y + (btn_h - 16) / 2; // 16 = textSize 2 height

        if (selected) {
            display.fillRect(bx, btn_y, btn_w, btn_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.drawRect(bx, btn_y, btn_w, btn_h, BLACK);
            display.setTextColor(BLACK);
        }
        display.setTextSize(2);
        display.setCursor(lx, ly);     display.print(buttons[i]);
        display.setCursor(lx + 1, ly); display.print(buttons[i]);
        display.setTextColor(BLACK);

        // Unread badge on TEXT button (index 0)
        if (i == 0 && unread > 0) {
            int badge_size = 24;
            int bx2 = bx + 2, by2 = btn_y + 2;
            if (selected) {
                display.fillRect(bx2, by2, badge_size, badge_size, WHITE);
                display.setTextColor(BLACK);
            } else {
                display.fillRect(bx2, by2, badge_size, badge_size, BLACK);
                display.setTextColor(WHITE);
            }
            char badge_label[2] = {'0' + (char)(unread > 9 ? 9 : unread), '\0'};
            int tx = bx2 + (badge_size - 12) / 2;
            int ty = by2 + (badge_size - 16) / 2;
            display.setTextSize(2);
            display.setCursor(tx, ty);
            display.print(badge_label);
            display.setTextColor(BLACK);
        }
    }
}

void render_msg_list(char* data, int selected) {
    // data = "Name·preview·time|Name·preview·time|..."
    const int header_h = 44;
    const int row_h    = 72;
    const int margin   = 16;

    // Header bar — outline style: < TEXT +
    display.drawLine(0, header_h - 1, 600, header_h - 1, BLACK);
    display.setTextSize(3);

    // < back — invert when selected == -1
    if (selected == -1) {
        int cw = 18, ch = 24;
        display.fillRect(margin - 4, 6, cw + 8, ch + 8, BLACK);
        display.setTextColor(WHITE);
        display.setCursor(margin, 10);
        display.print("<");
        display.setTextColor(BLACK);
    } else {
        display.setTextColor(BLACK);
        display.setCursor(margin, 10);
        display.print("<");
    }

    // TEXTS centered (5 chars * 18px = 90px wide)
    display.setTextColor(BLACK);
    display.setCursor((600 - 90) / 2, 10);
    display.print("TEXTS");

    // + right — invert when selected == -2
    int plus_x = 600 - margin - 18;
    if (selected == -2) {
        int cw = 18, ch = 24;
        display.fillRect(plus_x - 4, 6, cw + 8, ch + 8, BLACK);
        display.setTextColor(WHITE);
        display.setCursor(plus_x, 10);
        display.print("+");
        display.setTextColor(BLACK);
    } else {
        display.setTextColor(BLACK);
        display.setCursor(plus_x, 10);
        display.print("+");
    }

    int y = header_h;
    int row = 0;
    char* entry = data;

    while (entry != NULL && y + row_h <= 600) {
        char* next = strchr(entry, '|');
        char entry_buf[80] = "";
        if (next != NULL) {
            strncpy(entry_buf, entry, next - entry);
            entry = next + 1;
        } else {
            strncpy(entry_buf, entry, sizeof(entry_buf) - 1);
            entry = NULL;
        }

        // Parse name·preview·time
        char name_buf[16]    = "";
        char preview_buf[50] = "";
        char time_buf[12]    = "";

        char* dot1 = strchr(entry_buf, '\xB7');
        if (dot1 != NULL) {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(dot1 - entry_buf), entry_buf);
            char* dot2 = strchr(dot1 + 1, '\xB7');
            if (dot2 != NULL) {
                snprintf(preview_buf, sizeof(preview_buf), "%.*s", (int)(dot2 - dot1 - 1), dot1 + 1);
                strncpy(time_buf, dot2 + 1, sizeof(time_buf) - 1);
            } else {
                strncpy(preview_buf, dot1 + 1, sizeof(preview_buf) - 1);
            }
        } else {
            strncpy(name_buf, entry_buf, sizeof(name_buf) - 1);
        }

        bool is_sel = (row == selected);
        if (is_sel) {
            display.fillRect(0, y, 600, row_h, BLACK);
            display.setTextColor(WHITE);
        }

        // Name — textSize 3, left, bold
        display.setTextSize(3);
        display.setCursor(margin, y + 8);     display.print(name_buf);
        display.setCursor(margin + 1, y + 8); display.print(name_buf);

        // Chevron — textSize 2, right, vertically centered
        int chevron_x = 600 - margin - 12; // 12 = 1 char at textSize 2
        display.setTextSize(2);
        display.setCursor(chevron_x, y + (row_h - 16) / 2);
        display.print(">");

        // Timestamp — textSize 2, left of chevron
        if (strlen(time_buf) > 0) {
            int ts_w = strlen(time_buf) * 12; // textSize 2: 12px per char
            display.setCursor(chevron_x - ts_w - 8, y + 16);
            display.print(time_buf);
        }

        // Preview — textSize 2, left
        display.setCursor(margin, y + 40);
        display.print(preview_buf);

        if (is_sel) {
            display.setTextColor(BLACK);
        }

        display.drawLine(0, y + row_h - 1, 600, y + row_h - 1, BLACK);
        row++;
        y += row_h;
    }
}

// Render word-wrapped text, advancing *y by line_h per line.
// right_align=true: each line is right-aligned to right margin.
void render_wrapped(const char* text, bool right_align, int* y, int line_h) {
    const int margin = 20;
    const int char_w = 18; // textSize 3: 6*3
    const int max_chars = (600 - margin * 2) / char_w; // ~30 chars

    char line_buf[64] = "";
    const char* p = text;

    while (true) {
        // Find next word
        const char* word_start = p;
        while (*p && *p != ' ') p++;
        int word_len = p - word_start;

        if (word_len > 0) {
            int line_len = strlen(line_buf);
            bool fits = (line_len == 0) ? (word_len <= max_chars)
                                        : (line_len + 1 + word_len <= max_chars);
            if (fits) {
                if (line_len > 0) strcat(line_buf, " ");
                strncat(line_buf, word_start, word_len);
            } else {
                // Flush current line
                if (line_len > 0) {
                    int x = right_align ? (600 - margin - (int)strlen(line_buf) * char_w) : margin;
                    if (x < margin) x = margin;
                    display.setCursor(x, *y);
                    display.print(line_buf);
                    *y += line_h;
                    memset(line_buf, 0, sizeof(line_buf));
                }
                strncat(line_buf, word_start, word_len < 62 ? word_len : 62);
            }
        }

        if (*p == '\0') break;
        p++; // skip space
    }

    // Flush remaining
    if (strlen(line_buf) > 0) {
        int x = right_align ? (600 - margin - (int)strlen(line_buf) * char_w) : margin;
        if (x < margin) x = margin;
        display.setCursor(x, *y);
        display.print(line_buf);
        *y += line_h;
    }
}

void render_msg_thread(char* data) {
    // data = "Name|Y:time~body|R:time~body|..."
    char name_buf[32] = "";
    char* pipe = strchr(data, '|');
    if (pipe != NULL) {
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(pipe - data), data);
        data = pipe + 1;
    } else {
        strncpy(name_buf, data, sizeof(name_buf) - 1);
        data = NULL;
    }

    // Centered name header
    display.setTextSize(3);
    int name_x = (600 - (int)strlen(name_buf) * 18) / 2;
    if (name_x < 10) name_x = 10;
    display.setCursor(name_x, 10);     display.print(name_buf);
    display.setCursor(name_x + 1, 10); display.print(name_buf);
    display.drawLine(0, 46, 600, 46, BLACK);

    int y = 56;
    const int line_h = 32;   // textSize 3: 24px + 8px gap
    const int margin = 16;
    char last_time[12] = "";

    while (data != NULL && y < 570) {
        char* next = strchr(data, '|');
        char msg_buf[80] = "";
        if (next != NULL) {
            strncpy(msg_buf, data, next - data);
            data = next + 1;
        } else {
            strncpy(msg_buf, data, sizeof(msg_buf) - 1);
            data = NULL;
        }

        char align = 'R';
        char* rest = msg_buf;
        if (strlen(msg_buf) >= 2 && msg_buf[1] == ':') {
            align = msg_buf[0];
            rest = msg_buf + 2;
        }

        // Split time~body
        char time_buf[12] = "";
        char* body = rest;
        char* tilde = strchr(rest, '~');
        if (tilde != NULL) {
            strncpy(time_buf, rest, tilde - rest);
            body = tilde + 1;
        }

        // Time separator if new time
        if (strlen(time_buf) > 0 && strcmp(time_buf, last_time) != 0) {
            strncpy(last_time, time_buf, sizeof(last_time) - 1);
            display.setTextSize(1);
            int tw = strlen(time_buf) * 6;
            display.setCursor((600 - tw) / 2, y);
            display.print(time_buf);
            y += 16;
        }

        // AIM style: prefix on first line, body word-wrapped with continuation indent
        const char* label = (align == 'Y') ? "Me" : name_buf;
        char prefix_str[24] = "";
        snprintf(prefix_str, sizeof(prefix_str), "%s: ", label);
        int prefix_px = (int)strlen(prefix_str) * 18; // textSize 3: 18px/char
        int body_x = margin + prefix_px;
        int chars_per_line = (600 - body_x) / 18;
        if (chars_per_line < 1) chars_per_line = 1;

        display.setTextSize(3);
        display.setCursor(margin, y);     display.print(prefix_str);
        display.setCursor(margin + 1, y); display.print(prefix_str);

        char wbuf[48] = "";
        const char* p = body;
        while (*p != '\0') {
            while (*p == ' ') p++;
            if (*p == '\0') break;
            const char* ws = p;
            while (*p && *p != ' ') p++;
            int wlen = (int)(p - ws);
            int blen = (int)strlen(wbuf);
            bool fits = (blen == 0) ? (wlen <= chars_per_line)
                                    : (blen + 1 + wlen <= chars_per_line);
            if (fits) {
                if (blen > 0 && blen < (int)sizeof(wbuf) - 2) strcat(wbuf, " ");
                int avail = (int)sizeof(wbuf) - (int)strlen(wbuf) - 1;
                strncat(wbuf, ws, avail < wlen ? avail : wlen);
            } else {
                display.setCursor(body_x, y);
                display.print(wbuf);
                y += line_h;
                memset(wbuf, 0, sizeof(wbuf));
                strncat(wbuf, ws, wlen < (int)sizeof(wbuf) - 1 ? wlen : (int)sizeof(wbuf) - 1);
            }
        }
        display.setCursor(body_x, y);
        display.print(wbuf);
        y += line_h;
        y += 4;
    }
}

void render_sms(char* text) {
    // "SENDER|body" — two-line SMS format
    char* pipe = strchr(text, '|');
    if (pipe != NULL) {
        *pipe = '\0';
        display.setTextSize(3);
        display.setCursor(10, 10);
        display.print(text);
        display.setTextSize(4);
        display.setCursor(10, 60);
        display.print(pipe + 1);
    } else {
        display.setTextSize(4);
        display.setCursor(10, 10);
        display.print(text);
    }
}

// ══════════════════════════════════════════════════════════════
// OS 0.1 RENDERERS
// ══════════════════════════════════════════════════════════════

// Word-wrap text, centering each line. Returns final y after all lines.
int render_centered_wrapped(const char* text, int y, int line_h, int text_size) {
    const int char_w    = 6 * text_size;
    const int margin    = 60;
    const int max_chars = (600 - margin * 2) / char_w;
    char line_buf[80]   = "";
    const char* p = text;
    while (true) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        const char* ws = p;
        while (*p && *p != ' ') p++;
        int wlen = (int)(p - ws);
        if (wlen == 0) break;
        int blen = (int)strlen(line_buf);
        bool fits = (blen == 0) ? (wlen <= max_chars) : (blen + 1 + wlen <= max_chars);
        if (fits) {
            if (blen > 0 && blen < 78) strcat(line_buf, " ");
            int avail = 78 - (int)strlen(line_buf);
            strncat(line_buf, ws, avail < wlen ? avail : wlen);
        } else {
            if (blen > 0) {
                int x = (600 - (int)strlen(line_buf) * char_w) / 2;
                if (x < margin) x = margin;
                display.setCursor(x, y);
                display.print(line_buf);
                y += line_h;
                memset(line_buf, 0, sizeof(line_buf));
            }
            int avail = 78;
            strncat(line_buf, ws, avail < wlen ? avail : wlen);
        }
    }
    if (strlen(line_buf) > 0) {
        int x = (600 - (int)strlen(line_buf) * char_w) / 2;
        if (x < margin) x = margin;
        display.setCursor(x, y);
        display.print(line_buf);
        y += line_h;
    }
    return y;
}

void render_lock(char* data) {
    // data = "time_str|date_str|quote|attribution"
    char time_str[16] = "", date_str[32] = "", quote_buf[200] = "", attr_buf[40] = "";
    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(time_str, sizeof(time_str), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            snprintf(date_str, sizeof(date_str), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            char* p3 = strchr(p2 + 1, '|');
            if (p3) {
                snprintf(quote_buf, sizeof(quote_buf), "%.*s", (int)(p3 - p2 - 1), p2 + 1);
                strncpy(attr_buf, p3 + 1, sizeof(attr_buf) - 1);
            } else {
                strncpy(quote_buf, p2 + 1, sizeof(quote_buf) - 1);
            }
        } else {
            strncpy(date_str, p1 + 1, sizeof(date_str) - 1);
        }
    } else {
        strncpy(time_str, data, sizeof(time_str) - 1);
    }

    display.setTextColor(BLACK);

    // "OS 0.1" — top left, textSize 1
    display.setTextSize(1);
    display.setCursor(10, 8);
    display.print("OS 0.1");

    // ASCII cat — top right, textSize 2 (12px/char)
    const char* cat[] = {
        "   )\\._.,--....,'``.",
        "  /,   _.. \\   _\\  (`._ ,.",
        " `._.-(,_..'--(,_..'`-.;.'",
    };
    display.setTextSize(2);
    for (int ci = 0; ci < 3; ci++) {
        int lw = strlen(cat[ci]) * 12;
        int cx = 600 - lw - 6, cy = 6 + ci * 18;
        display.setCursor(cx, cy);     display.print(cat[ci]);
        display.setCursor(cx + 1, cy); display.print(cat[ci]);
    }

    // Clock — textSize 8 (48px/char, 64px tall), centered at y=130
    display.setTextSize(8);
    int clock_w = strlen(time_str) * 48;
    display.setCursor((600 - clock_w) / 2, 130);
    display.print(time_str);

    // Date — textSize 2 (12px/char), centered at y=210
    display.setTextSize(2);
    int date_w = strlen(date_str) * 12;
    display.setCursor((600 - date_w) / 2, 210);
    display.print(date_str);

    // Quote — word-wrapped, centered, starting at y=330
    display.setTextSize(2);
    int quote_end_y = render_centered_wrapped(quote_buf, 330, 20, 2);

    // Attribution — textSize 1, centered
    if (strlen(attr_buf) > 0) {
        display.setTextSize(1);
        int aw = strlen(attr_buf) * 6;
        display.setCursor((600 - aw) / 2, quote_end_y + 8);
        display.print(attr_buf);
    }
}

void render_home2(char* data) {
    // data = "time_str|home_index|unread"
    char time_str[16] = "";
    int home_index = 0, unread = 0;
    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(time_str, sizeof(time_str), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            char idx_buf[4] = "";
            snprintf(idx_buf, sizeof(idx_buf), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            home_index = atoi(idx_buf);
            unread     = atoi(p2 + 1);
        } else {
            home_index = atoi(p1 + 1);
        }
    } else {
        strncpy(time_str, data, sizeof(time_str) - 1);
    }

    const int header_h = 60;
    display.setTextColor(BLACK);

    // "KYPHONE" — left, textSize 2 (bold = double-print)
    display.setTextSize(2);
    display.setCursor(24, 18);  display.print("KYPHONE");
    display.setCursor(25, 18);  display.print("KYPHONE");

    // Clock — right, textSize 3 (18px/char)
    display.setTextSize(3);
    int tw = strlen(time_str) * 18;
    display.setCursor(600 - 24 - tw, 14);
    display.print(time_str);

    // Header rule (2px)
    display.drawLine(0, header_h,     600, header_h,     BLACK);
    display.drawLine(0, header_h + 1, 600, header_h + 1, BLACK);

    // 4 menu rows
    const char* labels[]  = {"TEXT", "CALL", "READ", "LISTEN"};
    const char* numbers[] = {"01",   "02",   "03",   "04"};
    const int row_h    = 80;
    const int pad_left = 64;
    const int num_w    = 24; // 2 chars at textSize 2 = 24px
    const int gap      = 20;
    const int label_x  = pad_left + num_w + gap; // 108

    for (int i = 0; i < 4; i++) {
        int y   = header_h + i * row_h;
        bool sel = (i == home_index);

        if (sel) {
            display.fillRect(0, y, 600, row_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }

        // Row number — textSize 2
        display.setTextSize(2);
        display.setCursor(pad_left, y + (row_h - 16) / 2 + 8);
        display.print(numbers[i]);

        // App label — textSize 4, bold (double-print)
        display.setTextSize(4);
        display.setCursor(label_x, y + (row_h - 32) / 2);
        display.print(labels[i]);
        display.setCursor(label_x + 1, y + (row_h - 32) / 2);
        display.print(labels[i]);

        // Unread badge on TEXT row
        if (i == 0 && unread > 0) {
            uint16_t bg = sel ? WHITE : BLACK;
            uint16_t fg = sel ? BLACK : WHITE;
            int bx = 600 - 50, by = y + (row_h - 24) / 2;
            display.fillRect(bx, by, 28, 24, bg);
            char badge[2] = {'0' + (char)(unread > 9 ? 9 : unread), '\0'};
            display.setTextSize(2);
            display.setTextColor(fg);
            display.setCursor(bx + 6, by + 4);
            display.print(badge);
        }

        display.setTextColor(BLACK);
        display.drawLine(0, y + row_h, 600, y + row_h, BLACK);
    }

    // Footer (2px rule + labels)
    const int footer_y = 550;
    display.drawLine(0, footer_y,     600, footer_y,     BLACK);
    display.drawLine(0, footer_y + 1, 600, footer_y + 1, BLACK);
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(28, footer_y + 14);
    display.print("BATT 82%");

    // Signal: 3 filled + 1 outlined rectangles
    for (int b = 0; b < 4; b++) {
        int bx = 600 - 28 - (4 - b) * 14;
        int by = footer_y + 8;
        if (b < 3) display.fillRect(bx, by, 10, 20, BLACK);
        else        display.drawRect(bx, by, 10, 20, BLACK);
    }
}

void render_texts(char* data, int selected) {
    // data = "name·preview·unread|..."  selected: -1=back, -2=plus, >=0=row
    const int header_h = 44;
    const int row_h    = 88;
    const int margin   = 28;

    display.drawLine(0, header_h - 1, 600, header_h - 1, BLACK);
    display.setTextSize(3);
    display.setTextColor(BLACK);

    // < back
    if (selected == -1) {
        display.fillRect(margin - 4, 6, 26, 32, BLACK);
        display.setTextColor(WHITE);
        display.setCursor(margin, 10); display.print("<");
        display.setTextColor(BLACK);
    } else {
        display.setCursor(margin, 10); display.print("<");
    }

    // "TEXT" centered (4 chars * 18px = 72px)
    display.setCursor((600 - 72) / 2, 10);     display.print("TEXT");
    display.setCursor((600 - 72) / 2 + 1, 10); display.print("TEXT");

    // + compose
    int plus_x = 600 - margin - 18;
    if (selected == -2) {
        display.fillRect(plus_x - 4, 6, 26, 32, BLACK);
        display.setTextColor(WHITE);
        display.setCursor(plus_x, 10); display.print("+");
        display.setTextColor(BLACK);
    } else {
        display.setCursor(plus_x, 10); display.print("+");
    }

    int y   = header_h;
    int row = 0;
    char* entry = data;

    while (entry != NULL && y + row_h <= 600) {
        char* next = strchr(entry, '|');
        char entry_buf[120] = "";
        if (next != NULL) {
            int len = (int)(next - entry);
            if (len >= 120) len = 119;
            strncpy(entry_buf, entry, len);
            entry = next + 1;
        } else {
            strncpy(entry_buf, entry, sizeof(entry_buf) - 1);
            entry = NULL;
        }

        char name_buf[16]    = "";
        char preview_buf[50] = "";
        char unread_buf[4]   = "0";

        char* dot1 = strchr(entry_buf, '\xB7');
        if (dot1 != NULL) {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(dot1 - entry_buf), entry_buf);
            char* dot2 = strchr(dot1 + 1, '\xB7');
            if (dot2 != NULL) {
                snprintf(preview_buf, sizeof(preview_buf), "%.*s", (int)(dot2 - dot1 - 1), dot1 + 1);
                strncpy(unread_buf, dot2 + 1, sizeof(unread_buf) - 1);
            } else {
                strncpy(preview_buf, dot1 + 1, sizeof(preview_buf) - 1);
            }
        } else {
            strncpy(name_buf, entry_buf, sizeof(name_buf) - 1);
        }
        bool is_unread = (unread_buf[0] == '1');
        bool is_sel    = (row == selected);

        if (is_sel) {
            display.fillRect(0, y, 600, row_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }

        // Name (bold if unread or selected)
        display.setTextSize(3);
        display.setCursor(margin, y + 10);
        display.print(name_buf);
        if (is_unread || is_sel) {
            display.setCursor(margin + 1, y + 10);
            display.print(name_buf);
        }

        // Chevron
        display.setTextSize(2);
        display.setCursor(600 - margin - 12, y + (row_h - 16) / 2);
        display.print(">");

        // Preview
        display.setCursor(margin, y + 52);
        display.print(preview_buf);

        display.setTextColor(BLACK);
        display.drawLine(0, y + row_h - 1, 600, y + row_h - 1, BLACK);
        row++;
        y += row_h;
    }
}

void render_thread2(char* data) {
    // data = "name|draft|Y:body|R:body|..."
    char name_buf[32]  = "";
    char draft_buf[44] = "";

    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(p1 - data), data);
        data = p1 + 1;
    } else {
        strncpy(name_buf, data, sizeof(name_buf) - 1);
        data = NULL;
    }
    if (data) {
        char* p2 = strchr(data, '|');
        if (p2) {
            snprintf(draft_buf, sizeof(draft_buf), "%.*s", (int)(p2 - data), data);
            data = p2 + 1;
        } else {
            strncpy(draft_buf, data, sizeof(draft_buf) - 1);
            data = NULL;
        }
    }

    // Header: < NAME i
    display.setTextSize(3);
    display.setTextColor(BLACK);
    display.setCursor(16, 10);
    display.print("<");
    int name_x = (600 - (int)strlen(name_buf) * 18) / 2;
    if (name_x < 10) name_x = 10;
    display.setCursor(name_x, 10);     display.print(name_buf);
    display.setCursor(name_x + 1, 10); display.print(name_buf);
    display.setCursor(600 - 16 - 18, 10);
    display.print("i");
    display.drawLine(0, 46, 600, 46, BLACK);

    // Reply bar at bottom (y=558)
    const int reply_y = 558;
    display.drawLine(0, reply_y,     600, reply_y,     BLACK);
    display.drawLine(0, reply_y + 1, 600, reply_y + 1, BLACK);
    display.setTextSize(3);
    display.setCursor(24, reply_y + 8);
    display.print("> ");
    int draft_x = 24 + 36; // "> " = 2 chars * 18px
    display.setCursor(draft_x, reply_y + 8);
    display.print(draft_buf);
    // Cursor block after draft text
    int cursor_x = draft_x + (int)strlen(draft_buf) * 18;
    display.fillRect(cursor_x, reply_y + 8, 18, 24, BLACK);

    // Messages in y=56 to reply_y-32
    int y          = 56;
    const int line_h = 32;
    const int margin = 16;
    char last_time[12] = "";

    while (data != NULL && y < reply_y - line_h) {
        char* next = strchr(data, '|');
        char msg_buf[80] = "";
        if (next != NULL) {
            int len = (int)(next - data);
            if (len >= 80) len = 79;
            strncpy(msg_buf, data, len);
            data = next + 1;
        } else {
            strncpy(msg_buf, data, sizeof(msg_buf) - 1);
            data = NULL;
        }

        char align = 'R';
        char* rest = msg_buf;
        if (strlen(msg_buf) >= 2 && msg_buf[1] == ':') {
            align = msg_buf[0];
            rest  = msg_buf + 2;
        }
        char time_buf[12] = "";
        char* body = rest;
        char* tilde = strchr(rest, '~');
        if (tilde) { strncpy(time_buf, rest, tilde - rest); body = tilde + 1; }

        if (strlen(time_buf) > 0 && strcmp(time_buf, last_time) != 0) {
            strncpy(last_time, time_buf, sizeof(last_time) - 1);
            display.setTextSize(1);
            int tw = strlen(time_buf) * 6;
            display.setCursor((600 - tw) / 2, y);
            display.print(time_buf);
            y += 16;
        }

        const char* label = (align == 'Y') ? "Me" : name_buf;
        char prefix_str[24] = "";
        snprintf(prefix_str, sizeof(prefix_str), "%s: ", label);
        int prefix_px = (int)strlen(prefix_str) * 18;
        int body_x    = margin + prefix_px;
        int cpl       = (600 - body_x) / 18;
        if (cpl < 1) cpl = 1;

        display.setTextSize(3);
        display.setTextColor(BLACK);
        display.setCursor(margin, y);     display.print(prefix_str);
        display.setCursor(margin + 1, y); display.print(prefix_str);

        char wbuf[48] = "";
        const char* bp = body;
        while (*bp != '\0') {
            while (*bp == ' ') bp++;
            if (*bp == '\0') break;
            const char* ws2 = bp;
            while (*bp && *bp != ' ') bp++;
            int wlen = (int)(bp - ws2);
            int blen = (int)strlen(wbuf);
            bool fits = (blen == 0) ? (wlen <= cpl) : (blen + 1 + wlen <= cpl);
            if (fits) {
                if (blen > 0 && blen < 46) strcat(wbuf, " ");
                int avail = 47 - (int)strlen(wbuf);
                strncat(wbuf, ws2, avail < wlen ? avail : wlen);
            } else {
                display.setCursor(body_x, y);
                display.print(wbuf);
                y += line_h;
                if (y >= reply_y - line_h) break;
                memset(wbuf, 0, sizeof(wbuf));
                strncat(wbuf, ws2, wlen < 47 ? wlen : 47);
            }
        }
        if (strlen(wbuf) > 0 && y < reply_y - line_h) {
            display.setCursor(body_x, y);
            display.print(wbuf);
            y += line_h;
        }
        y += 4;
    }
}

void render_compose(char* data) {
    // data = "compose_to|compose_msg|to_active"
    char to_buf[64]  = "";
    char msg_buf[80] = "";
    char to_active   = '1';

    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(to_buf, sizeof(to_buf), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            snprintf(msg_buf, sizeof(msg_buf), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            to_active = *(p2 + 1);
        } else {
            strncpy(msg_buf, p1 + 1, sizeof(msg_buf) - 1);
        }
    } else {
        strncpy(to_buf, data, sizeof(to_buf) - 1);
    }

    display.setTextColor(BLACK);

    // "NEW MESSAGE" header — textSize 3, bold
    display.setTextSize(3);
    display.setCursor(24, 10);     display.print("NEW MESSAGE");
    display.setCursor(25, 10);     display.print("NEW MESSAGE");
    display.drawLine(0, 44, 600, 44, BLACK);
    display.drawLine(0, 45, 600, 45, BLACK);

    // TO: label — textSize 1
    display.setTextSize(1);
    display.setCursor(24, 58);
    display.print("TO:");

    // TO field — textSize 3
    display.setTextSize(3);
    display.setCursor(24, 72);
    display.print(to_buf);
    if (to_active == '1') {
        int cx = 24 + (int)strlen(to_buf) * 18;
        display.fillRect(cx, 72, 18, 24, BLACK);
    }
    display.drawLine(0, 100, 600, 100, BLACK);

    // MESSAGE: label — textSize 1
    display.setTextSize(1);
    display.setCursor(24, 114);
    display.print("MESSAGE:");

    // Message field — textSize 2, word-wrapped
    display.setTextSize(2);
    const int char_w = 12, cpl = (600 - 48) / char_w;
    int y_pos = 128;
    char wbuf2[64] = "";
    const char* mp = msg_buf;
    while (*mp != '\0') {
        while (*mp == ' ') mp++;
        if (*mp == '\0') break;
        const char* ws2 = mp;
        while (*mp && *mp != ' ') mp++;
        int wlen = (int)(mp - ws2);
        int blen = (int)strlen(wbuf2);
        bool fits = (blen == 0) ? (wlen <= cpl) : (blen + 1 + wlen <= cpl);
        if (fits) {
            if (blen > 0 && blen < 62) strcat(wbuf2, " ");
            int avail = 62 - (int)strlen(wbuf2);
            strncat(wbuf2, ws2, avail < wlen ? avail : wlen);
        } else {
            display.setCursor(24, y_pos);
            display.print(wbuf2);
            y_pos += 20;
            memset(wbuf2, 0, sizeof(wbuf2));
            int avail = 62;
            strncat(wbuf2, ws2, avail < wlen ? avail : wlen);
        }
    }
    display.setCursor(24, y_pos);
    display.print(wbuf2);
    if (to_active != '1') {
        int cx = 24 + (int)strlen(wbuf2) * char_w;
        display.fillRect(cx, y_pos, char_w, 16, BLACK);
    }
}

void render_stub(char* data) {
    // data = "app_name"
    char name_buf[32] = "";
    strncpy(name_buf, data, sizeof(name_buf) - 1);
    char* pipe = strchr(name_buf, '|');
    if (pipe) *pipe = '\0';

    display.setTextSize(3);
    display.setTextColor(BLACK);
    display.setCursor(24, 10);     display.print(name_buf);
    display.setCursor(25, 10);     display.print(name_buf);
    display.drawLine(0, 44, 600, 44, BLACK);
    display.drawLine(0, 45, 600, 45, BLACK);

    char coming[64] = "";
    snprintf(coming, sizeof(coming), "%s: COMING SOON", name_buf);
    display.setTextSize(2);
    int tw = strlen(coming) * 12;
    display.setCursor((600 - tw) / 2, 280);
    display.print(coming);
}

void setup() {
    Serial.begin(115200);
    delay(2000); 
    Serial.println("\n--- KYPHONE SPI: V4 (NOISE CANCELLER) ---");

    display.begin();
    display.einkOff();

    reclaim_spi_pins_for_gpio();

    pinMode(PIN_SCLK, INPUT_PULLDOWN);
    pinMode(PIN_MOSI, INPUT_PULLDOWN);

    attachInterrupt(digitalPinToInterrupt(PIN_SCLK), spi_clock_isr, RISING);
    // CS ISR disabled: noise on Pin 15 resets bit_counter mid-transfer.
    // SCLK-timeout framing handles message boundaries instead.

    display.expander2.pinMode(8, OUTPUT, true);
    display.expander2.digitalWrite(8, LOW, true);
    delay(10);
    display.expander2.digitalWrite(8, HIGH, true);
    Serial.println(">> Handshake set HIGH");
    Serial.println(">> SYSTEM READY. Send message now.");
}

void loop() {
    static uint32_t last_debug = 0;
    uint32_t now = millis();

    // Atomic snapshot — prevents ISR updating last_sclk_time AFTER now_us is captured,
    // which causes unsigned underflow (now_us - last_sclk_time wraps to ~4B > 1500000).
    noInterrupts();
    uint32_t snap_sclk  = last_sclk_time;
    uint32_t snap_first = first_sclk_time;
    uint16_t snap_bits  = bit_counter;
    uint32_t now_us     = micros();
    interrupts();

    if (now - last_debug > 2000) {
        last_debug = now;
        int cs_val = digitalRead(PIN_CS);
        Serial.printf("DEBUG [%lums]: SCLK_Total: %d | Bits: %d | CS: %d | Screen: %s\n",
            millis(), debug_sclk_total, snap_bits, cs_val, current_screen);
    }

    // Framing: 1500ms silence = end of message (410ms transfer at 5kHz for 256 bytes + margin)
    if (!transfer_complete && snap_bits > 0) {
        if (now_us - snap_sclk > 600000) {
            uint32_t elapsed_us = now_us - snap_first;
            if (snap_bits == TOTAL_BITS) {
                transfer_complete = true;
                Serial.printf(">> COMPLETE: %d bits in %lums\n", snap_bits, elapsed_us / 1000);
            } else if (snap_bits > TOTAL_BITS) {
                Serial.printf(">> OVERFLOW: %d bits in %lums. Resetting.\n", snap_bits, elapsed_us / 1000);
                bit_counter = 0;
            } else {
                Serial.printf(">> PARTIAL: %d/%d bits in %lums. Resetting.\n", snap_bits, TOTAL_BITS, elapsed_us / 1000);
                bit_counter = 0;
            }
        }
    }

    if (transfer_complete) {
        display.expander2.digitalWrite(8, LOW, true);
        
        uint8_t local_buf[PAYLOAD_BYTES];
        memcpy(local_buf, (void*)rx_buf, PAYLOAD_BYTES);
        transfer_complete = false;
        bit_counter = 0;
        
        Serial.println(">> MESSAGE CAPTURED!");

        int offset = -1;
        for(int i=0; i<PAYLOAD_BYTES; i++) {
            if(local_buf[i] == 0x02 || local_buf[i] == 0x03) { offset = i; break; }
        }

        if (offset != -1) {
            uint8_t marker = local_buf[offset];

            if (marker == 0x03) {
                // --- Display region update (partial refresh) ---
                if (offset + 9 >= PAYLOAD_BYTES) {
                    Serial.println(">> ERROR: 0x03 marker too close to buffer end, skipping");
                    goto done_processing;
                }
                uint8_t flags = local_buf[offset+1];
                bool last_chunk = (flags & 0x01) == 0x00;
                int x = (local_buf[offset+2] << 8) | local_buf[offset+3];
                int y = (local_buf[offset+4] << 8) | local_buf[offset+5];
                int w = (local_buf[offset+6] << 8) | local_buf[offset+7];
                int h = (local_buf[offset+8] << 8) | local_buf[offset+9];
                uint8_t* pixels = &local_buf[offset+10];
                int byte_idx = 0;
                int bit_idx = 7;

                Serial.printf("REGION: (%d,%d) %dx%d last=%d\n", x, y, w, h, last_chunk);

                for (int py = y; py < y + h && byte_idx < PAYLOAD_BYTES; py++) {
                    for (int px = x; px < x + w && byte_idx < PAYLOAD_BYTES; px++) {
                        bool white = (pixels[byte_idx] >> bit_idx) & 0x1;
                        display.drawPixel(px, py, white ? WHITE : BLACK);
                        if (--bit_idx < 0) { bit_idx = 7; byte_idx++; }
                    }
                }

                if (last_chunk) {
                    display.partialUpdate();
                    reclaim_spi_pins_for_gpio();
                }

            } else {
                // --- 0x02: screen command or SMS ---
                char* text = (char*)&local_buf[offset+1];
                Serial.printf("SUCCESS! MSG: %s\n", text);

                if (strncmp(text, "HOME_FAST|", 10) == 0) {
                    strncpy(current_screen, "HOME", sizeof(current_screen) - 1);
                    display.clearDisplay();
                    render_home(text + 10);
                    display.partialUpdate();
                    delay(100);
                    display.einkOff();
                    reclaim_spi_pins_for_gpio();
                } else if (strncmp(text, "MSG_LIST_FAST|", 14) == 0) {
                    strncpy(current_screen, "MSG_LIST", sizeof(current_screen) - 1);
                    char* after = text + 14;
                    int sel = 0;
                    char* idx_end = strchr(after, '|');
                    if (idx_end != NULL) {
                        char idx_buf[4] = "";
                        strncpy(idx_buf, after, idx_end - after);
                        sel = atoi(idx_buf);
                        after = idx_end + 1;
                    }
                    display.clearDisplay();
                    render_msg_list(after, sel);
                    display.partialUpdate();
                    delay(100);
                    display.einkOff();
                    reclaim_spi_pins_for_gpio();
                } else {
                    display.clearDisplay();
                    if (strncmp(text, "HOME|", 5) == 0) {
                        strncpy(current_screen, "HOME", sizeof(current_screen) - 1);
                        render_home(text + 5);
                    } else if (strncmp(text, "MSG_LIST|", 9) == 0) {
                        strncpy(current_screen, "MSG_LIST", sizeof(current_screen) - 1);
                        // First token after MSG_LIST| is the selected index
                        char* after = text + 9;
                        int sel = 0;
                        char* idx_end = strchr(after, '|');
                        if (idx_end != NULL) {
                            char idx_buf[4] = "";
                            strncpy(idx_buf, after, idx_end - after);
                            sel = atoi(idx_buf);
                            after = idx_end + 1;
                        }
                        render_msg_list(after, sel);
                    } else if (strncmp(text, "MSG_THREAD|", 11) == 0) {
                        // Extract contact name for log
                        char thread_name[32] = "";
                        const char* tn = text + 11;
                        const char* tn_end = strchr(tn, '|');
                        if (tn_end) snprintf(thread_name, sizeof(thread_name), "%.*s", (int)(tn_end - tn), tn);
                        else        strncpy(thread_name, tn, sizeof(thread_name) - 1);
                        snprintf(current_screen, sizeof(current_screen), "MSG_THREAD(%s)", thread_name);
                        render_msg_thread(text + 11);
                    // ── OS 0.1 commands ──────────────────────────────────────
                    } else if (strncmp(text, "LOCK|", 5) == 0) {
                        strncpy(current_screen, "LOCK", sizeof(current_screen) - 1);
                        render_lock(text + 5);
                    } else if (strncmp(text, "HOME2|", 6) == 0) {
                        strncpy(current_screen, "HOME2", sizeof(current_screen) - 1);
                        render_home2(text + 6);
                    } else if (strncmp(text, "TEXTS|", 6) == 0) {
                        strncpy(current_screen, "TEXTS", sizeof(current_screen) - 1);
                        char* after   = text + 6;
                        int   sel     = 0;
                        char* idx_end = strchr(after, '|');
                        if (idx_end != NULL) {
                            char idx_buf[5] = "";
                            int len = (int)(idx_end - after);
                            if (len >= 5) len = 4;
                            strncpy(idx_buf, after, len);
                            sel   = atoi(idx_buf);
                            after = idx_end + 1;
                        }
                        render_texts(after, sel);
                    } else if (strncmp(text, "THREAD2|", 8) == 0) {
                        char thread_name[32] = "";
                        const char* tn = text + 8;
                        const char* tn_end = strchr(tn, '|');
                        if (tn_end) snprintf(thread_name, sizeof(thread_name), "%.*s", (int)(tn_end - tn), tn);
                        else        strncpy(thread_name, tn, sizeof(thread_name) - 1);
                        snprintf(current_screen, sizeof(current_screen), "THREAD2(%s)", thread_name);
                        render_thread2(text + 8);
                    } else if (strncmp(text, "COMPOSE|", 8) == 0) {
                        strncpy(current_screen, "COMPOSE", sizeof(current_screen) - 1);
                        render_compose(text + 8);
                    } else if (strncmp(text, "STUB|", 5) == 0) {
                        strncpy(current_screen, "STUB", sizeof(current_screen) - 1);
                        render_stub(text + 5);
                    } else {
                        strncpy(current_screen, "SMS", sizeof(current_screen) - 1);
                        render_sms(text);
                    }
                    page_transition_count++;
                    if (page_transition_count >= 10) {
                        display.display();
                        page_transition_count = 0;
                        Serial.println(">> Full refresh (ghost clear)");
                    } else {
                        display.partialUpdate();
                    }
                    delay(100);
                    display.einkOff();
                    reclaim_spi_pins_for_gpio();
                }
            }
        } else {
            Serial.println(">> ERROR: No Header (0x02). Check MOSI wiring.");
            Serial.printf("Raw Data: %02X %02X %02X %02X\n", local_buf[0], local_buf[1], local_buf[2], local_buf[3]);
        }

        done_processing:
        display.expander2.digitalWrite(8, HIGH, true);
    }
}