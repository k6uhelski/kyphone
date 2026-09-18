#include <Inkplate.h>
#include <driver/gpio.h>
#include "soc/io_mux_reg.h"
#include <ctype.h>

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
unsigned long last_full_refresh_ms = 0;  // time-based full refresh cadence (ghost clear)
#define FULL_REFRESH_INTERVAL_MS 600000UL  // full refresh every 10 minutes; partialUpdate() otherwise
bool did_boot_full_refresh = false;  // force one full refresh on the first screen after boot —
                                      // the panel may still show a stale image from before this
                                      // flash, and partialUpdate() alone won't clear it
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

// ASCII cat, pre-rendered to a fixed 1-bit bitmap (Courier Bold) so it always
// looks identical regardless of the display's live text-rendering behavior.
// Generated from the same art previously drawn with display.print().
#define CAT_BITMAP_W 322
#define CAT_BITMAP_H 80

const uint8_t cat_bitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x30, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3C, 0x03, 0xC0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x06, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF0, 0x3E, 0x03, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x03, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, 0x83, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x03, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x0F,
    0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x81, 0xC0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xF0, 0x03, 0x80, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x81, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0xE0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x80, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7F, 0xE7, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x78, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xE7, 0xFE, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x80, 0x78, 0x07, 0x00, 0x00, 0x07, 0x00, 0xF8, 0x7F, 0xE7,
    0xFE, 0x07, 0x00, 0x70, 0x07, 0x00, 0x70, 0x0F, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x3C, 0x0F, 0x80, 0x00,
    0x0F, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x0F, 0x80, 0xF8, 0x0F, 0x80, 0xF8,
    0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x80, 0x3C, 0x1F, 0x80, 0x00, 0x1F, 0x80, 0xF0, 0x00, 0x00, 0x00, 0x1F,
    0x81, 0xF8, 0x1F, 0x81, 0xF8, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x1E, 0x0F, 0x80, 0x00, 0x0F, 0x81,
    0xF0, 0x00, 0x00, 0x00, 0x0F, 0x80, 0xF8, 0x0F, 0x80, 0xF8, 0x1F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x1E,
    0x0F, 0x00, 0x00, 0x0F, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x0F, 0x00, 0xF0,
    0x0F, 0x00, 0xF0, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x80, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80, 0x01, 0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x03, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x03, 0xF8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x00, 0x0E, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xE0, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
    0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0xE0, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x07, 0x00,
    0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00,
    0x00, 0x1C, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x07, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF8, 0x0F, 0x80, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x0F, 0x80, 0x00,
    0x00, 0x00, 0xF8, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x0F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xF8, 0x1F, 0x80, 0x00, 0x03,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x1C,
    0x00, 0x00, 0x1F, 0x80, 0x00, 0x00, 0x00, 0xF0, 0x1F, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x80, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF8, 0x0F, 0x80, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x01,
    0xF0, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x1E, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0x00, 0x00, 0x01, 0xE0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00,
    0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80, 0x01, 0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF,
    0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF,
    0xF8, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x03, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0F, 0x03, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x03, 0xF8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0F, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x01,
    0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x06, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xE1, 0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0xFE, 0x7F, 0xE1, 0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xE0, 0x00, 0x0F, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F,
    0xE1, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFE, 0x7F,
    0xE1, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F,
    0xE0, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x00, 0x70, 0x7F, 0xE1, 0xC0, 0x0F, 0x80, 0x00, 0x07, 0x00,
    0x70, 0x00, 0x07, 0xFE, 0x7F, 0xE1, 0xC0, 0x0F, 0x80, 0x00, 0x07, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x7F, 0xE0, 0x70, 0x0F, 0x00, 0x70, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0xF8, 0x00, 0x01, 0xC0,
    0x0F, 0x80, 0x00, 0x0F, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC0,
    0x0F, 0x80, 0x00, 0x0F, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8,
    0x0F, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xF8, 0x00,
    0x01, 0xF8, 0x00, 0x01, 0xC0, 0x0F, 0x00, 0x00, 0x1F, 0x81, 0xF8, 0x00,
    0x00, 0x00, 0x00, 0x01, 0xC0, 0x0F, 0x00, 0x00, 0x1F, 0x81, 0xF8, 0x00,
    0x00, 0x00, 0x00, 0x01, 0xF8, 0x1F, 0x01, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0xF8, 0x00, 0x01, 0xC0, 0x1F, 0x00,
    0x00, 0x0F, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC0, 0x1F, 0x00,
    0x00, 0x0F, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x1F, 0x00,
    0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0xF0,
    0x00, 0x01, 0xE0, 0x1E, 0x00, 0x00, 0x0F, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x01, 0xE0, 0x1E, 0x00, 0x00, 0x0F, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x1E, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x1E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x1E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE0, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE0, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0xFF, 0xF8, 0x00, 0x00, 0x00, 0xF0, 0x1C, 0x1F, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x1C, 0x1F, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x60, 0x00,
    0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00,
    0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

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

    // "OS 0.2" — bottom left, textSize 2 (18px design token)
    display.setTextSize(2);
    display.setCursor(10, 600 - 8 - 16);
    display.print("OS 0.2");

    // ASCII cat — bottom right, drawn as a fixed bitmap (see cat_bitmap above); unchanged since 0.1
    display.drawBitmap(600 - CAT_BITMAP_W - 6, 600 - CAT_BITMAP_H - 6, cat_bitmap, CAT_BITMAP_W, CAT_BITMAP_H, BLACK);

    // Clock — textSize 8, centered, top:159
    display.setTextSize(8);
    int clock_w = strlen(time_str) * 48;
    display.setCursor((600 - clock_w) / 2, 159);
    display.print(time_str);

    // Date — textSize 3, centered, top:243
    display.setTextSize(3);
    int date_w = strlen(date_str) * 18;
    display.setCursor((600 - date_w) / 2, 243);
    display.print(date_str);

    // Quote — textSize 3, centered, wrapped, top:356, line-height 34
    display.setTextSize(3);
    int quote_end_y = render_centered_wrapped(quote_buf, 356, 34, 3);

    // Attribution — textSize 2, centered, 12px below the quote block
    if (strlen(attr_buf) > 0) {
        display.setTextSize(2);
        int aw = strlen(attr_buf) * 12;
        display.setCursor((600 - aw) / 2, quote_end_y + 12);
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
    bool header_sel = (home_index == -1);
    uint16_t header_fg = header_sel ? WHITE : BLACK;
    uint16_t header_bg = header_sel ? BLACK : WHITE;

    if (header_sel) {
        display.fillRect(0, 0, 600, header_h, BLACK);
    }
    display.setTextColor(header_fg);

    // Clock — left, textSize 3. No "KYPHONE" label in 0.2.
    display.setTextSize(3);
    display.setCursor(24, 18);
    display.print(time_str);

    render_status_group(header_fg, header_bg, header_h / 2);

    display.setTextColor(BLACK);
    display.drawLine(0, header_h,     600, header_h,     BLACK);
    display.drawLine(0, header_h + 1, 600, header_h + 1, BLACK);

    // 5-row menu (TEXT/CALL/READ/LISTEN/CONTACTS), scrolling: 4 fill the
    // panel, the 5th scrolls into view when selected.
    const char* labels[] = {"TEXT", "CALL", "READ", "LISTEN", "CONTACTS"};
    const int n         = 5;
    const int row_h     = 135;
    const int view_top  = header_h + 2;
    const int view_h    = 600 - view_top;
    int clamped_idx     = home_index > 0 ? home_index : 0;
    int shift           = (clamped_idx + 1) * row_h - view_h;
    if (shift < 0) shift = 0;

    for (int i = 0; i < n; i++) {
        int y = view_top + i * row_h - shift;
        if (y + row_h < view_top || y > 600) continue;
        bool sel = (i == home_index);

        if (sel) {
            int fill_y = y < view_top ? view_top : y;
            int fill_h = (y + row_h > 600 ? 600 : y + row_h) - fill_y;
            display.fillRect(0, fill_y, 600, fill_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }

        // Label — textSize 6, bold (double-print), centered (icons are a
        // separate task — see planning/kyphone_backlog.md)
        display.setTextSize(6);
        int label_w = strlen(labels[i]) * 36; // 6px/char * textSize 6
        int label_x = (600 - label_w) / 2;
        int label_y = y + (row_h - 48) / 2;
        display.setCursor(label_x, label_y);     display.print(labels[i]);
        display.setCursor(label_x + 1, label_y); display.print(labels[i]);

        // Unread count hangs to the right of the TEXT row's content
        if (strcmp(labels[i], "TEXT") == 0 && unread > 0) {
            char count[4];
            snprintf(count, sizeof(count), "%d", unread > 99 ? 99 : unread);
            display.setTextSize(3);
            display.setCursor(label_x + label_w + 24, label_y + (48 - 24) / 2);
            display.print(count);
        }

        display.setTextColor(BLACK);
        if (y + row_h <= 600) display.drawLine(0, y + row_h, 600, y + row_h, BLACK);
    }

    // "More below" chevron — three shrinking bars, bottom right
    if (n > 4 && home_index <= 3) {
        int cx = 600 - 12, cy = 600 - 6;
        int widths[] = {14, 8, 3};
        for (int i = 0; i < 3; i++) {
            display.fillRect(cx - widths[i], cy - 3, widths[i], 3, BLACK);
            cy -= 5;
        }
    }
}

// Battery block + percentage + 4-bar signal staircase, right-aligned in the
// home header. No real telemetry exists yet — fixed placeholder values,
// swappable for real readings later.
void render_status_group(uint16_t fg, uint16_t bg, int mid_y) {
    const int batt_pct = 82;
    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", batt_pct);
    int pct_w = strlen(pct_str) * 18; // textSize 3

    const int sig_heights[] = {5, 9, 13, 17};
    const int sig_w = 4, sig_gap = 3;
    int sig_group_w = sig_w * 4 + sig_gap * 3;

    const int batt_w = 34, batt_h = 18, batt_border = 2;
    const int nub_w = 3, nub_h = 8;

    int total_w = batt_w + 2 + nub_w + 14 + pct_w + 14 + sig_group_w;
    int x = 600 - 24 - total_w;

    int by = mid_y - batt_h / 2;
    display.drawRect(x, by, batt_w, batt_h, fg);
    display.drawRect(x + 1, by + 1, batt_w - 2, batt_h - 2, fg);
    int fill_w = (int)((batt_w - 2 * batt_border) * (batt_pct / 100.0));
    display.fillRect(x + batt_border, by + batt_border, fill_w, batt_h - 2 * batt_border, fg);
    x += batt_w + 2;
    display.fillRect(x, mid_y - nub_h / 2, nub_w, nub_h, fg);
    x += nub_w + 14;

    display.setTextSize(3);
    display.setTextColor(fg);
    display.setCursor(x, mid_y - 12);
    display.print(pct_str);
    x += pct_w + 14;

    int sig_bottom = mid_y + sig_heights[3] / 2;
    for (int i = 0; i < 4; i++) {
        int h = sig_heights[i];
        if (i == 3) {
            display.drawRect(x, sig_bottom - h, sig_w, h, fg);
        } else {
            display.fillRect(x, sig_bottom - h, sig_w, h, fg);
        }
        x += sig_w + sig_gap;
    }
}

// Shared back/title/+ header used by list screens — each control is a
// literal 38x34 hit box that inverts when selected.
void render_header_bar(const char* title, bool back_active, bool plus_active, int height) {
    display.drawLine(0, height - 1, 600, height - 1, BLACK);
    display.setTextColor(BLACK);
    const int box_w = 38, box_h = 34;

    if (back_active) {
        display.fillRect(16, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    }
    display.setTextSize(3);
    display.setCursor(16 + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("<");
    display.setTextColor(BLACK);

    int title_w = strlen(title) * 18;
    display.setCursor((600 - title_w) / 2, 10);     display.print(title);
    display.setCursor((600 - title_w) / 2 + 1, 10); display.print(title);

    int plus_x = 600 - 16 - box_w;
    if (plus_active) {
        display.fillRect(plus_x, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    }
    display.setCursor(plus_x + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("+");
    display.setTextColor(BLACK);
}

void render_texts(char* data, int selected) {
    // data = "name·preview·unread·time|..."  selected: -1=back, -2=plus, >=0=row
    const int row_h  = 88;
    const int margin = 28;

    render_header_bar("TEXT", selected == -1, selected == -2, 44);

    int y   = 44;
    int row = 0;
    char* entry = data;

    while (entry != NULL && y + row_h <= 600) {
        char* next = strchr(entry, '|');
        char entry_buf[140] = "";
        if (next != NULL) {
            int len = (int)(next - entry);
            if (len >= (int)sizeof(entry_buf)) len = sizeof(entry_buf) - 1;
            strncpy(entry_buf, entry, len);
            entry = next + 1;
        } else {
            strncpy(entry_buf, entry, sizeof(entry_buf) - 1);
            entry = NULL;
        }

        char name_buf[16]    = "";
        char preview_buf[50] = "";
        char unread_buf[4]   = "0";
        char time_buf[16]    = "";

        char* dot1 = strchr(entry_buf, '\xB7');
        if (dot1 != NULL) {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(dot1 - entry_buf), entry_buf);
            char* dot2 = strchr(dot1 + 1, '\xB7');
            if (dot2 != NULL) {
                snprintf(preview_buf, sizeof(preview_buf), "%.*s", (int)(dot2 - dot1 - 1), dot1 + 1);
                char* dot3 = strchr(dot2 + 1, '\xB7');
                if (dot3 != NULL) {
                    snprintf(unread_buf, sizeof(unread_buf), "%.*s", (int)(dot3 - dot2 - 1), dot2 + 1);
                    strncpy(time_buf, dot3 + 1, sizeof(time_buf) - 1);
                } else {
                    strncpy(unread_buf, dot2 + 1, sizeof(unread_buf) - 1);
                }
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

        // Name (left) — bold if unread
        display.setTextSize(3);
        display.setCursor(margin, y + 10);
        display.print(name_buf);
        if (is_unread) {
            display.setCursor(margin + 1, y + 10);
            display.print(name_buf);
        }

        // Time + chevron (right), baseline-aligned with the name
        display.setTextSize(2);
        int chevron_x = 600 - margin - 12;
        display.setCursor(chevron_x, y + 14);
        display.print(">");
        if (strlen(time_buf) > 0) {
            int time_w = strlen(time_buf) * 12;
            display.setCursor(chevron_x - time_w - 10, y + 14);
            display.print(time_buf);
        }

        // Preview
        display.setCursor(margin, y + 48);
        display.print(preview_buf);

        display.setTextColor(BLACK);
        display.drawLine(0, y + row_h - 1, 600, y + row_h - 1, BLACK);
        row++;
        y += row_h;
    }
}

// Word-wrap text into fixed-size line buffers (max_chars per line), up to
// max_lines. Returns the number of lines produced (always >= 1).
int wrap_into_lines(const char* text, int max_chars, char lines_out[][32], int max_lines) {
    if (max_chars > 31) max_chars = 31;
    int n = 0;
    char cur[32] = "";
    const char* p = text;
    while (*p != '\0' && n < max_lines) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        const char* ws = p;
        while (*p && *p != ' ') p++;
        int wlen = (int)(p - ws);
        int blen = (int)strlen(cur);
        bool fits = (blen == 0) ? (wlen <= max_chars) : (blen + 1 + wlen <= max_chars);
        if (fits) {
            if (blen > 0 && blen < 30) strcat(cur, " ");
            int avail = 31 - (int)strlen(cur);
            strncat(cur, ws, avail < wlen ? avail : wlen);
        } else {
            strncpy(lines_out[n], cur, 31); lines_out[n][31] = '\0'; n++;
            memset(cur, 0, sizeof(cur));
            if (n >= max_lines) break;
            strncat(cur, ws, wlen < 31 ? wlen : 31);
        }
    }
    if (strlen(cur) > 0 && n < max_lines) {
        strncpy(lines_out[n], cur, 31); lines_out[n][31] = '\0'; n++;
    }
    if (n == 0) { lines_out[0][0] = '\0'; n = 1; }
    return n;
}

void render_thread2(char* data) {
    // data = "name|draft|hdr|Y:body|R:body|..."  hdr: ''=typing 'B'=back 'I'=info
    char name_buf[32]  = "";
    char draft_buf[44] = "";
    char hdr_buf[2]    = "";

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
    if (data) {
        char* p3 = strchr(data, '|');
        if (p3) {
            snprintf(hdr_buf, sizeof(hdr_buf), "%.*s", (int)(p3 - data), data);
            data = p3 + 1;
        } else {
            strncpy(hdr_buf, data, sizeof(hdr_buf) - 1);
            data = NULL;
        }
    }

    // Header: < NAME i
    const int box_w = 38, box_h = 34;
    bool back_sel = (hdr_buf[0] == 'B');
    bool info_sel = (hdr_buf[0] == 'I');

    display.setTextSize(3);
    if (back_sel) {
        display.fillRect(16, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setCursor(16 + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("<");

    display.setTextColor(BLACK);
    int name_x = (600 - (int)strlen(name_buf) * 18) / 2;
    if (name_x < 10) name_x = 10;
    display.setCursor(name_x, 10);     display.print(name_buf);
    display.setCursor(name_x + 1, 10); display.print(name_buf);

    int info_x = 600 - 16 - box_w;
    if (info_sel) {
        display.fillRect(info_x, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setCursor(info_x + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("i");
    display.setTextColor(BLACK);
    display.drawLine(0, 46, 600, 46, BLACK);

    // Composer — pinned to the bottom 45px
    const int composer_h = 45;
    const int reply_y    = 600 - composer_h;
    display.drawLine(0, reply_y,     600, reply_y,     BLACK);
    display.drawLine(0, reply_y + 1, 600, reply_y + 1, BLACK);
    int field_y = reply_y + (composer_h - 24) / 2;
    display.setCursor(24, field_y);
    display.print("> ");
    int draft_x = 24 + 2 * 18;
    display.setCursor(draft_x, field_y);
    display.print(draft_buf);
    int cursor_x = draft_x + (int)strlen(draft_buf) * 18;
    display.fillRect(cursor_x, field_y, 18, 24, BLACK);

    // Messages — 2px-bordered bubbles stacked from the bottom. Incoming
    // left/paper with a stepped pixel tail and the sender's name above
    // each incoming run; outgoing right/filled ink. Time sits under every
    // bubble. Up to 4 messages arrive over the wire (see push_thread2 in
    // kyphone_os.py); only as many as fit the region are drawn.
    const int MAX_BLOCKS = 4, MAX_LINES = 6;
    struct ThreadBlock {
        char align;
        char time_buf[12];
        char lines[MAX_LINES][32];
        int  num_lines;
        bool show_name;
        int  bubble_h;
        int  total_h;
    };
    ThreadBlock blocks[MAX_BLOCKS];
    int nblocks = 0;

    const int pad_x = 12, pad_y = 8;
    const int bubble_line_h = 35;   // 24px text at line-height 1.45
    const int max_bubble_w  = 400;
    const int inner_cpl     = (max_bubble_w - 2 * pad_x) / 18;

    char prev_align = 0;
    while (data != NULL && nblocks < MAX_BLOCKS) {
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
        if (strlen(msg_buf) == 0) continue;

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

        ThreadBlock& b = blocks[nblocks];
        b.align = align;
        strncpy(b.time_buf, time_buf, sizeof(b.time_buf) - 1);
        b.time_buf[sizeof(b.time_buf) - 1] = '\0';
        b.num_lines  = wrap_into_lines(body, inner_cpl, b.lines, MAX_LINES);
        b.show_name  = (align != 'Y') && (prev_align != align);
        b.bubble_h   = pad_y * 2 + b.num_lines * bubble_line_h;
        b.total_h    = b.bubble_h + 8 * 2 + 4;
        if (b.show_name) b.total_h += 8 * 2 + 6;
        prev_align = align;
        nblocks++;
    }

    const int margin       = 16;
    const int tail_widths[] = {4, 8, 14, 8, 4};
    const int tail_seg_h   = 4;
    const int tail_span    = 14 + 2;
    const int gap          = 16;
    const int top = 62, bottom = 600 - 62;

    // Keep the newest blocks that fit the message region, bottom-up.
    int used = 0, first_fit = nblocks;
    for (int i = nblocks - 1; i >= 0; i--) {
        int extra = blocks[i].total_h + (first_fit < nblocks ? gap : 0);
        if (used + extra > bottom - top) break;
        used += extra;
        first_fit = i;
    }

    int y = bottom - used;
    for (int i = first_fit; i < nblocks; i++) {
        ThreadBlock& b = blocks[i];
        int block_top = y;
        int y0 = y;

        if (b.show_name) {
            char upper[32];
            strncpy(upper, name_buf, sizeof(upper) - 1);
            upper[sizeof(upper) - 1] = '\0';
            for (char* c = upper; *c; c++) *c = toupper((unsigned char)*c);
            display.setTextSize(2);
            display.setTextColor(BLACK);
            display.setCursor(margin + tail_span, y0);     display.print(upper);
            display.setCursor(margin + tail_span + 1, y0); display.print(upper);
            y0 += 8 * 2 + 6;
        }

        int line_w = 0;
        for (int li = 0; li < b.num_lines; li++) {
            int lw = (int)strlen(b.lines[li]) * 18;
            if (lw > line_w) line_w = lw;
        }
        int bw = line_w + 2 * pad_x;
        if (bw > max_bubble_w) bw = max_bubble_w;
        bool outgoing = (b.align == 'Y');
        int bx = outgoing ? (600 - margin - bw) : (margin + tail_span);

        uint16_t fill    = outgoing ? BLACK : WHITE;
        uint16_t text_fg = outgoing ? WHITE : BLACK;
        display.fillRect(bx, y0, bw, b.bubble_h, fill);
        display.drawRect(bx, y0, bw, b.bubble_h, BLACK);
        display.drawRect(bx + 1, y0 + 1, bw - 2, b.bubble_h - 2, BLACK);

        display.setTextSize(3);
        display.setTextColor(text_fg);
        int ly = y0 + pad_y;
        for (int li = 0; li < b.num_lines; li++) {
            display.setCursor(bx + pad_x, ly);
            display.print(b.lines[li]);
            ly += bubble_line_h;
        }

        int tail_total_h = tail_seg_h * 5;
        int tail_y = y0 + (b.bubble_h - tail_total_h) / 2;
        for (int ti = 0; ti < 5; ti++) {
            int w  = tail_widths[ti];
            int tx = outgoing ? (bx + bw + 2) : (bx - 2 - w);
            display.fillRect(tx, tail_y + ti * tail_seg_h, w, tail_seg_h, BLACK);
        }

        display.setTextSize(2);
        display.setTextColor(BLACK);
        int time_w = (int)strlen(b.time_buf) * 12;
        int time_x = outgoing ? (bx + bw - time_w) : bx;
        display.setCursor(time_x, y0 + b.bubble_h + 6);
        display.print(b.time_buf);

        y = block_top + b.total_h + gap;
    }
}

// N-px border — Adafruit_GFX's drawRect is always 1px, so a thicker border
// is N nested rects.
void draw_thick_rect(int x, int y, int w, int h, int thickness, uint16_t color) {
    for (int i = 0; i < thickness; i++) {
        display.drawRect(x + i, y + i, w - 2 * i, h - 2 * i, color);
    }
}

// A field label that inverts (fills ink, text flips to paper) while its
// field is active — the mode is marked at the field, not just by cursor
// position. textSize 2.
void render_field_label(const char* text, int x, int y, bool active) {
    int w = (int)strlen(text) * 12 + 4;
    int h = 16 + 4;
    if (active) {
        display.fillRect(x - 2, y - 2, w, h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setTextSize(2);
    display.setCursor(x, y);
    display.print(text);
    display.setTextColor(BLACK);
}

void render_compose(char* data) {
    // data = "to|msg|to_active|hdr|plus_sel|send_sel"  hdr: ''=typing 'X'=exit
    char to_buf[64] = "", msg_buf[80] = "", hdr_buf[2] = "";
    char to_active = '1', plus_sel_c = '0', send_sel_c = '0';

    char* fields[6] = {0};
    int nf = 0;
    char* tok = data;
    fields[nf++] = tok;
    while (nf < 6) {
        char* pipe = strchr(tok, '|');
        if (!pipe) break;
        *pipe = '\0';
        tok = pipe + 1;
        fields[nf++] = tok;
    }
    if (nf > 0) strncpy(to_buf, fields[0], sizeof(to_buf) - 1);
    if (nf > 1) strncpy(msg_buf, fields[1], sizeof(msg_buf) - 1);
    if (nf > 2) to_active = fields[2][0];
    if (nf > 3) strncpy(hdr_buf, fields[3], sizeof(hdr_buf) - 1);
    if (nf > 4) plus_sel_c = fields[4][0];
    if (nf > 5) send_sel_c = fields[5][0];

    bool x_sel      = (hdr_buf[0] == 'X');
    bool plus_sel   = (plus_sel_c == '1');
    bool send_sel   = (send_sel_c == '1');
    bool to_is_active = (to_active == '1');
    const int box_w = 38, box_h = 34;

    display.setTextColor(BLACK);
    display.setTextSize(3);
    display.setCursor(24, 10); display.print("NEW MESSAGE");
    display.setCursor(25, 10); display.print("NEW MESSAGE");

    int xb_x = 600 - 16 - box_w, xb_y = 8;
    if (x_sel) {
        display.fillRect(xb_x, xb_y, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setCursor(xb_x + (box_w - 18) / 2, xb_y + (box_h - 24) / 2);
    display.print("X");
    display.setTextColor(BLACK);
    display.drawLine(0, 44, 600, 44, BLACK);
    display.drawLine(0, 45, 600, 45, BLACK);

    // TO: field
    render_field_label("TO:", 24, 58, to_is_active);
    display.setTextSize(3);
    display.setTextColor(BLACK);
    display.setCursor(24, 84);
    display.print(to_buf);

    if (to_is_active && strlen(to_buf) == 0) {
        int px = 600 - 24 - box_w, py = 79;
        if (plus_sel) {
            display.fillRect(px, py, box_w, box_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }
        display.setCursor(px + (box_w - 18) / 2, py + (box_h - 24) / 2);
        display.print("+");
        display.setTextColor(BLACK);
    } else if (to_is_active && !plus_sel) {
        int cx = 24 + (int)strlen(to_buf) * 18;
        display.fillRect(cx, 84, 18, 24, BLACK);
    }
    display.drawLine(0, 122, 600, 122, BLACK);

    // MESSAGE: field — textSize 3, word-wrapped
    render_field_label("MESSAGE:", 24, 134, !to_is_active);
    bool msg_active = !to_is_active && !send_sel;
    display.setTextSize(3);
    display.setTextColor(BLACK);
    const int cpl = (600 - 48) / 18;
    int y_pos = 162;
    char wbuf2[64] = "", last_line[64] = "";
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
            strncpy(last_line, wbuf2, sizeof(last_line) - 1);
            y_pos += 34;
            memset(wbuf2, 0, sizeof(wbuf2));
            int avail = 62;
            strncat(wbuf2, ws2, avail < wlen ? avail : wlen);
        }
    }
    display.setCursor(24, y_pos);
    display.print(wbuf2);
    strncpy(last_line, wbuf2, sizeof(last_line) - 1);
    if (msg_active) {
        int cx = 24 + (int)strlen(last_line) * 18;
        display.fillRect(cx, y_pos, 12, 16, BLACK);
    }

    // SEND — bottom right; activates the same as Enter on a filled-out message
    const char* send_label = "SEND";
    int send_w = (int)strlen(send_label) * 12 + 36;
    int send_h = 16 + 12;
    int send_x = 600 - 24 - send_w;
    int send_y = 600 - 14 - send_h;
    if (send_sel) {
        display.fillRect(send_x, send_y, send_w, send_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    draw_thick_rect(send_x, send_y, send_w, send_h, 3, BLACK);
    display.setTextSize(2);
    display.setCursor(send_x + 18, send_y + 6);
    display.print(send_label);
    display.setTextColor(BLACK);
}

// Boxed '!' + prose paragraph — the alert pattern used by stub screens and
// the discard confirmation: what happened, why, what to do instead, all in
// one paragraph.
void render_alert_icon_body(int top, const char* body) {
    const int icon_size = 46, icon_x = 56;
    display.setTextColor(BLACK);
    draw_thick_rect(icon_x, top, icon_size, icon_size, 2, BLACK);
    display.setTextSize(3);
    display.setCursor(icon_x + (icon_size - 18) / 2, top + (icon_size - 24) / 2);
    display.print("!");

    int text_x = icon_x + icon_size + 22;
    int max_px = 600 - 56 - text_x;
    render_wrapped_lines_at(body, text_x, top, max_px, 34, 3);
}

// Word-wraps text at textSize `ts` within `max_px`, drawing left-aligned
// lines starting at (x, y) with the given line height.
void render_wrapped_lines_at(const char* text, int x, int y, int max_px, int line_h, int ts) {
    const int char_w = 6 * ts;
    int max_chars = max_px / char_w;
    if (max_chars < 1) max_chars = 1;
    char line_buf[80] = "";
    const char* p = text;
    display.setTextSize(ts);
    while (true) {
        const char* ws = p;
        while (*p && *p != ' ') p++;
        int wlen = (int)(p - ws);
        if (wlen > 0) {
            int blen = (int)strlen(line_buf);
            bool fits = (blen == 0) ? (wlen <= max_chars) : (blen + 1 + wlen <= max_chars);
            if (fits) {
                if (blen > 0) strcat(line_buf, " ");
                int avail = 78 - (int)strlen(line_buf);
                strncat(line_buf, ws, avail < wlen ? avail : wlen);
            } else {
                display.setCursor(x, y);
                display.print(line_buf);
                y += line_h;
                memset(line_buf, 0, sizeof(line_buf));
                int avail = 78;
                strncat(line_buf, ws, avail < wlen ? avail : wlen);
            }
        }
        if (*p == '\0') break;
        p++;
    }
    if (strlen(line_buf) > 0) {
        display.setCursor(x, y);
        display.print(line_buf);
    }
}

void render_stub(char* data) {
    // data = "title|body"
    char title[32] = "", body[220] = "";
    char* pipe = strchr(data, '|');
    if (pipe) {
        snprintf(title, sizeof(title), "%.*s", (int)(pipe - data), data);
        strncpy(body, pipe + 1, sizeof(body) - 1);
    } else {
        strncpy(title, data, sizeof(title) - 1);
    }

    display.setTextColor(BLACK);
    display.setTextSize(3);
    display.setCursor(24, 10); display.print(title);
    display.setCursor(25, 10); display.print(title);
    display.drawLine(0, 44, 600, 44, BLACK);

    render_alert_icon_body(212, body);

    const char* label = "OK";
    int w = (int)strlen(label) * 12 + 36, h = 16 + 12;
    int x = 600 - 24 - w, y = 600 - 24 - h;
    draw_thick_rect(x, y, w, h, 3, BLACK);
    display.setTextSize(2);
    display.setCursor(x + 18, y + 6);
    display.print(label);
}

void render_confirm_discard(char* data) {
    // data = "K" (keep) or "D" (discard)
    bool discard_sel = (data[0] == 'D');

    display.setTextColor(BLACK);
    display.setTextSize(3);
    display.setCursor(24, 10); display.print("NEW MESSAGE");
    display.setCursor(25, 10); display.print("NEW MESSAGE");
    display.drawLine(0, 44, 600, 44, BLACK);

    render_alert_icon_body(196,
        "DISCARD THIS MESSAGE? IT HAS NOT BEEN SENT, AND THE PHONE KEEPS "
        "NO DRAFTS, SO THE TEXT CANNOT BE BROUGHT BACK.");

    const char* d_label = "DISCARD";
    int d_w = (int)strlen(d_label) * 12 + 36, d_h = 16 + 14;
    int d_x = 56, d_y = 600 - 24 - d_h;
    if (discard_sel) {
        display.fillRect(d_x, d_y, d_w, d_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    draw_thick_rect(d_x, d_y, d_w, d_h, 2, BLACK);
    display.setTextSize(2);
    display.setCursor(d_x + 18, d_y + 7);
    display.print(d_label);
    display.setTextColor(BLACK);

    const char* k_label = "KEEP EDITING";
    int k_w = (int)strlen(k_label) * 12 + 36, k_h = 16 + 12;
    int k_x = 600 - 24 - k_w, k_y = 600 - 24 - k_h;
    if (!discard_sel) {
        display.fillRect(k_x, k_y, k_w, k_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    draw_thick_rect(k_x, k_y, k_w, k_h, 3, BLACK);
    display.setCursor(k_x + 18, k_y + 6);
    display.print(k_label);
    display.setTextColor(BLACK);
}

void render_contacts_pick(char* data) {
    // data = "idx|query|name·number|..."  idx: -1=back, -2=plus, >=0=row
    char* p1 = strchr(data, '|');
    int idx = 0;
    char query[40] = "";
    char* entries = NULL;
    if (p1) {
        char idx_buf[6] = "";
        snprintf(idx_buf, sizeof(idx_buf), "%.*s", (int)(p1 - data), data);
        idx = atoi(idx_buf);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            snprintf(query, sizeof(query), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            entries = p2 + 1;
        } else {
            strncpy(query, p1 + 1, sizeof(query) - 1);
        }
    }

    render_header_bar("CONTACTS", idx == -1, idx == -2, 44);

    const int row_h = 64, bar_h = 45;
    int y = 44, row = 0;
    char* entry = entries;
    while (entry != NULL && y + row_h <= 600 - bar_h) {
        char* next = strchr(entry, '|');
        char entry_buf[80] = "";
        if (next != NULL) {
            int len = (int)(next - entry);
            if (len >= (int)sizeof(entry_buf)) len = sizeof(entry_buf) - 1;
            strncpy(entry_buf, entry, len);
            entry = next + 1;
        } else {
            strncpy(entry_buf, entry, sizeof(entry_buf) - 1);
            entry = NULL;
        }

        char name_buf[32] = "", number_buf[24] = "";
        char* dot = strchr(entry_buf, '\xB7');
        if (dot) {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(dot - entry_buf), entry_buf);
            strncpy(number_buf, dot + 1, sizeof(number_buf) - 1);
        } else {
            strncpy(name_buf, entry_buf, sizeof(name_buf) - 1);
        }

        bool sel = (row == idx);
        if (sel) {
            display.fillRect(0, y, 600, row_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }
        display.setTextSize(3);
        display.setCursor(28, y + (row_h - 24) / 2);
        display.print(name_buf);

        display.setTextSize(2);
        int num_w = (int)strlen(number_buf) * 12;
        display.setCursor(600 - 28 - num_w, y + (row_h - 16) / 2);
        display.print(number_buf);

        display.setTextColor(BLACK);
        display.drawLine(0, y + row_h - 1, 600, y + row_h - 1, BLACK);
        row++;
        y += row_h;
    }

    int bar_y = 600 - bar_h;
    display.drawLine(0, bar_y,     600, bar_y,     BLACK);
    display.drawLine(0, bar_y + 1, 600, bar_y + 1, BLACK);
    display.setTextSize(2);
    display.setCursor(28, bar_y + (bar_h - 16) / 2);
    display.print("LOOK UP:");
    int qx = 28 + 8 * 12 + 14;
    display.setTextSize(3);
    display.setCursor(qx, bar_y + (bar_h - 24) / 2);
    display.print(query);
    int cursor_x = qx + (int)strlen(query) * 18;
    display.fillRect(cursor_x, bar_y + (bar_h - 24) / 2, 18, 24, BLACK);
}

void render_contact(char* data) {
    // data = "name|number|sel"  sel: B=back C=call T=text E=edit
    char name_buf[32] = "", number_buf[24] = "NO NUMBER SAVED";
    char sel = 'C';
    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            snprintf(number_buf, sizeof(number_buf), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            sel = *(p2 + 1);
        } else {
            strncpy(number_buf, p1 + 1, sizeof(number_buf) - 1);
        }
    } else {
        strncpy(name_buf, data, sizeof(name_buf) - 1);
    }

    const int box_w = 38, box_h = 34;
    bool back_sel = (sel == 'B'), edit_sel = (sel == 'E');
    bool call_sel = (sel == 'C'), text_sel = (sel == 'T');

    if (back_sel) {
        display.fillRect(16, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setTextSize(3);
    display.setCursor(16 + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("<");
    display.setTextColor(BLACK);

    int title_w = 7 * 18; // "CONTACT"
    display.setCursor((600 - title_w) / 2, 10);     display.print("CONTACT");
    display.setCursor((600 - title_w) / 2 + 1, 10); display.print("CONTACT");

    const char* edit_label = "EDIT";
    int edit_w = (int)strlen(edit_label) * 12 + 20, edit_h = 34;
    int edit_x = 600 - 16 - edit_w;
    if (edit_sel) {
        display.fillRect(edit_x, 6, edit_w, edit_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setTextSize(2);
    display.setCursor(edit_x + 10, 6 + (edit_h - 16) / 2);
    display.print(edit_label);
    display.setTextColor(BLACK);
    display.drawLine(0, 43, 600, 43, BLACK);

    display.setTextSize(6);
    display.setCursor(28, 150);
    display.print(name_buf);
    display.setTextSize(3);
    display.setCursor(28, 150 + 48 + 18);
    display.print(number_buf);
    display.drawLine(28, 330, 600 - 28, 330, BLACK);

    const int btn_y = 360, btn_h = 24 + 16;
    const char* call_label = "CALL";
    int call_w = (int)strlen(call_label) * 18 + 44;
    if (call_sel) {
        display.fillRect(28, btn_y, call_w, btn_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    draw_thick_rect(28, btn_y, call_w, btn_h, 3, BLACK);
    display.setTextSize(3);
    display.setCursor(28 + 22, btn_y + 8);
    display.print(call_label);
    display.setTextColor(BLACK);

    int text_x = 28 + call_w + 16;
    const char* text_label = "TEXT";
    int text_w = (int)strlen(text_label) * 18 + 44;
    if (text_sel) {
        display.fillRect(text_x, btn_y, text_w, btn_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    draw_thick_rect(text_x, btn_y, text_w, btn_h, 3, BLACK);
    display.setCursor(text_x + 22, btn_y + 8);
    display.print(text_label);
    display.setTextColor(BLACK);
}

void render_contact_edit(char* data) {
    // data = "first|last|number|idx"  idx: -1=cancel, 0/1/2=field, 3=save
    char first[16] = "", last[16] = "", number[24] = "";
    int idx = 0;
    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(first, sizeof(first), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            snprintf(last, sizeof(last), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            char* p3 = strchr(p2 + 1, '|');
            if (p3) {
                snprintf(number, sizeof(number), "%.*s", (int)(p3 - p2 - 1), p2 + 1);
                idx = atoi(p3 + 1);
            } else {
                strncpy(number, p2 + 1, sizeof(number) - 1);
            }
        } else {
            strncpy(last, p1 + 1, sizeof(last) - 1);
        }
    } else {
        strncpy(first, data, sizeof(first) - 1);
    }

    const int box_w = 38, box_h = 34;
    bool cancel_sel = (idx == -1);
    if (cancel_sel) {
        display.fillRect(16, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setTextSize(3);
    display.setCursor(16 + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("X");
    display.setTextColor(BLACK);

    int title_w = 12 * 18; // "EDIT CONTACT"
    display.setCursor((600 - title_w) / 2, 10);     display.print("EDIT CONTACT");
    display.setCursor((600 - title_w) / 2 + 1, 10); display.print("EDIT CONTACT");
    display.drawLine(0, 43, 600, 43, BLACK);

    struct FieldSpec { const char* label; const char* value; int y_label, y_value, y_rule; bool active; };
    FieldSpec specs[3] = {
        {"FIRST NAME:",    first,  70,  100, 138, idx == 0},
        {"LAST NAME:",     last,   158, 188, 226, idx == 1},
        {"PHONE NUMBER:",  number, 246, 276, 314, idx == 2},
    };
    for (int i = 0; i < 3; i++) {
        render_field_label(specs[i].label, 24, specs[i].y_label, specs[i].active);
        display.setTextSize(3);
        display.setTextColor(BLACK);
        display.setCursor(24, specs[i].y_value);
        display.print(specs[i].value);
        if (specs[i].active) {
            int cx = 24 + (int)strlen(specs[i].value) * 18;
            display.fillRect(cx, specs[i].y_value, 18, 24, BLACK);
        }
        display.drawLine(0, specs[i].y_rule, 600, specs[i].y_rule, BLACK);
    }

    bool save_sel = (idx == 3);
    const char* label = "SAVE";
    int w = (int)strlen(label) * 12 + 36, h = 16 + 12;
    int x = 600 - 24 - w, y = 600 - 14 - h;
    if (save_sel) {
        display.fillRect(x, y, w, h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    draw_thick_rect(x, y, w, h, 3, BLACK);
    display.setTextSize(2);
    display.setCursor(x + 18, y + 6);
    display.print(label);
    display.setTextColor(BLACK);
}

void render_calls(char* data) {
    // data = "idx|name·tag·time·duration|..."  idx: -1=back, 0=DIAL A NUMBER, 1+=log
    char* p1 = strchr(data, '|');
    int idx = 0;
    char* entries = NULL;
    if (p1) {
        char idx_buf[6] = "";
        snprintf(idx_buf, sizeof(idx_buf), "%.*s", (int)(p1 - data), data);
        idx = atoi(idx_buf);
        entries = p1 + 1;
    } else {
        idx = atoi(data);
    }

    display.drawLine(0, 43, 600, 43, BLACK);
    const int box_w = 38, box_h = 34;
    bool back_sel = (idx == -1);
    if (back_sel) {
        display.fillRect(16, 6, box_w, box_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setTextSize(3);
    display.setCursor(16 + (box_w - 18) / 2, 6 + (box_h - 24) / 2);
    display.print("<");
    display.setTextColor(BLACK);
    int title_w = 4 * 18; // "CALL"
    display.setCursor((600 - title_w) / 2, 10);     display.print("CALL");
    display.setCursor((600 - title_w) / 2 + 1, 10); display.print("CALL");

    const int row_h = 76;
    int y = 44;
    bool sel = (idx == 0);
    if (sel) {
        display.fillRect(0, y, 600, row_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }
    display.setCursor(28, y + (row_h - 24) / 2);
    display.print("DIAL A NUMBER");
    display.setTextColor(BLACK);
    display.drawLine(0, y + row_h - 1, 600, y + row_h - 1, BLACK);
    y += row_h;

    int row = 0;
    char* entry = entries;
    while (entry != NULL && y + row_h <= 600) {
        char* next = strchr(entry, '|');
        char entry_buf[80] = "";
        if (next != NULL) {
            int len = (int)(next - entry);
            if (len >= (int)sizeof(entry_buf)) len = sizeof(entry_buf) - 1;
            strncpy(entry_buf, entry, len);
            entry = next + 1;
        } else {
            strncpy(entry_buf, entry, sizeof(entry_buf) - 1);
            entry = NULL;
        }

        char name_buf[24] = "", tag_buf[8] = "", time_buf[20] = "", dur_buf[10] = "";
        char* d1 = strchr(entry_buf, '\xB7');
        if (d1) {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(d1 - entry_buf), entry_buf);
            char* d2 = strchr(d1 + 1, '\xB7');
            if (d2) {
                snprintf(tag_buf, sizeof(tag_buf), "%.*s", (int)(d2 - d1 - 1), d1 + 1);
                char* d3 = strchr(d2 + 1, '\xB7');
                if (d3) {
                    snprintf(time_buf, sizeof(time_buf), "%.*s", (int)(d3 - d2 - 1), d2 + 1);
                    strncpy(dur_buf, d3 + 1, sizeof(dur_buf) - 1);
                } else {
                    strncpy(time_buf, d2 + 1, sizeof(time_buf) - 1);
                }
            } else {
                strncpy(tag_buf, d1 + 1, sizeof(tag_buf) - 1);
            }
        } else {
            strncpy(name_buf, entry_buf, sizeof(name_buf) - 1);
        }

        sel = ((row + 1) == idx);
        if (sel) {
            display.fillRect(0, y, 600, row_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }
        display.setTextSize(3);
        display.setCursor(28, y + (row_h - 24) / 2);
        display.print(name_buf);

        int tag_x = 28 + (int)strlen(name_buf) * 18 + 12;
        int tag_w = (int)strlen(tag_buf) * 12 + 12;
        display.drawRect(tag_x, y + (row_h - 16) / 2, tag_w, 16, sel ? WHITE : BLACK);
        display.setTextSize(2);
        display.setCursor(tag_x + 6, y + (row_h - 16) / 2);
        display.print(tag_buf);

        int t_w = (int)strlen(time_buf) * 12;
        display.setCursor(600 - 28 - t_w, y + (row_h - 38) / 2);
        display.print(time_buf);
        int dur_w = (int)strlen(dur_buf) * 12;
        display.setCursor(600 - 28 - dur_w, y + (row_h - 38) / 2 + 22);
        display.print(dur_buf);

        display.setTextColor(BLACK);
        display.drawLine(0, y + row_h - 1, 600, y + row_h - 1, BLACK);
        row++;
        y += row_h;
    }
}

void render_dial(char* data) {
    // data = "buffer|quick_idx|name|..."
    char buf[24] = "";
    int qidx = -1;
    char* names = NULL;
    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(buf, sizeof(buf), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            char idx_buf[6] = "";
            snprintf(idx_buf, sizeof(idx_buf), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            qidx = atoi(idx_buf);
            names = p2 + 1;
        } else {
            qidx = atoi(p1 + 1);
        }
    } else {
        strncpy(buf, data, sizeof(buf) - 1);
    }

    display.setTextColor(BLACK);
    display.setTextSize(2);
    int label_w = 4 * 12; // "DIAL"
    display.setCursor((600 - label_w) / 2, 40);
    display.print("DIAL");

    display.setTextSize(6);
    int buf_w = (int)strlen(buf) * 36, cursor_w = 28;
    int x = (600 - buf_w - cursor_w) / 2;
    display.setCursor(x, 70);
    display.print(buf);
    display.fillRect(x + buf_w, 70, cursor_w, 48, BLACK);
    display.drawLine(0, 170,     600, 170,     BLACK);
    display.drawLine(0, 171,     600, 171,     BLACK);
    display.setTextSize(2);
    display.setCursor(28, 186);
    display.print("RECENT");

    const int row_h = 56;
    int y = 210;
    char* name = names;
    int i = 0;
    while (name != NULL && y + row_h <= 600) {
        char* next = strchr(name, '|');
        char name_buf[24] = "";
        if (next != NULL) {
            int len = (int)(next - name);
            if (len >= (int)sizeof(name_buf)) len = sizeof(name_buf) - 1;
            strncpy(name_buf, name, len);
            name = next + 1;
        } else {
            strncpy(name_buf, name, sizeof(name_buf) - 1);
            name = NULL;
        }
        bool sel = (i == qidx);
        if (sel) {
            display.fillRect(28, y, 600 - 56, row_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }
        display.setTextSize(3);
        display.setCursor(32, y + (row_h - 24) / 2);
        display.print(name_buf);
        display.setTextColor(BLACK);
        display.drawLine(28, y + row_h - 1, 600 - 28, y + row_h - 1, BLACK);
        i++;
        y += row_h;
    }
}

void render_call_state(char* data) {
    // data = "OUT|IN|ACTIVE" + "|name|timer"
    char state_buf[8] = "", name_buf[32] = "", timer_buf[8] = "00:00";
    char* p1 = strchr(data, '|');
    if (p1) {
        snprintf(state_buf, sizeof(state_buf), "%.*s", (int)(p1 - data), data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2) {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)(p2 - p1 - 1), p1 + 1);
            strncpy(timer_buf, p2 + 1, sizeof(timer_buf) - 1);
        } else {
            strncpy(name_buf, p1 + 1, sizeof(name_buf) - 1);
        }
    } else {
        strncpy(state_buf, data, sizeof(state_buf) - 1);
    }

    bool incoming = (strcmp(state_buf, "IN") == 0);
    bool active   = (strcmp(state_buf, "ACTIVE") == 0);
    uint16_t fg = incoming ? WHITE : BLACK;
    if (incoming) display.fillRect(0, 0, 600, 600, BLACK);
    display.setTextColor(fg);

    const char* label = incoming ? "INCOMING CALL" : (active ? "IN CALL" : "CALLING...");
    display.setTextSize(2);
    int label_w = (int)strlen(label) * 12;
    display.setCursor((600 - label_w) / 2, 220);
    display.print(label);

    display.setTextSize(6);
    int name_w = (int)strlen(name_buf) * 36;
    display.setCursor((600 - name_w) / 2, 280);
    display.print(name_buf);

    if (active) {
        display.setTextSize(3);
        int tw = (int)strlen(timer_buf) * 18;
        display.setCursor((600 - tw) / 2, 360);
        display.print(timer_buf);
    }

    const char* hint = incoming ? "ENTER ACCEPT / ESC DECLINE" : "ESC HANG UP";
    display.setTextSize(2);
    int hint_w = (int)strlen(hint) * 12;
    display.setCursor((600 - hint_w) / 2, 600 - 34 - 16);
    display.print(hint);
    display.setTextColor(BLACK);
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
                    } else if (strncmp(text, "CONFIRMDISCARD|", 15) == 0) {
                        strncpy(current_screen, "CONFIRMDISCARD", sizeof(current_screen) - 1);
                        render_confirm_discard(text + 15);
                    } else if (strncmp(text, "CONTACTSPICK|", 13) == 0) {
                        strncpy(current_screen, "CONTACTSPICK", sizeof(current_screen) - 1);
                        render_contacts_pick(text + 13);
                    } else if (strncmp(text, "CONTACTEDIT|", 12) == 0) {
                        strncpy(current_screen, "CONTACTEDIT", sizeof(current_screen) - 1);
                        render_contact_edit(text + 12);
                    } else if (strncmp(text, "CONTACT|", 8) == 0) {
                        strncpy(current_screen, "CONTACT", sizeof(current_screen) - 1);
                        render_contact(text + 8);
                    } else if (strncmp(text, "CALLS|", 6) == 0) {
                        strncpy(current_screen, "CALLS", sizeof(current_screen) - 1);
                        render_calls(text + 6);
                    } else if (strncmp(text, "DIAL|", 5) == 0) {
                        strncpy(current_screen, "DIAL", sizeof(current_screen) - 1);
                        render_dial(text + 5);
                    } else if (strncmp(text, "CALLSTATE|", 10) == 0) {
                        strncpy(current_screen, "CALLSTATE", sizeof(current_screen) - 1);
                        render_call_state(text + 10);
                    } else {
                        strncpy(current_screen, "SMS", sizeof(current_screen) - 1);
                        render_sms(text);
                    }
                    unsigned long now_ms = millis();
                    if (!did_boot_full_refresh || now_ms - last_full_refresh_ms >= FULL_REFRESH_INTERVAL_MS) {
                        display.display();
                        last_full_refresh_ms   = now_ms;
                        did_boot_full_refresh  = true;
                        Serial.println(">> Full refresh (ghost clear, time-based)");
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