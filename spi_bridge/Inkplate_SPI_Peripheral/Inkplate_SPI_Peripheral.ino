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
volatile uint16_t bit_counter = 0;
volatile bool transfer_complete = false;
volatile uint32_t last_sclk_time = 0;
volatile uint32_t last_cs_time = 0;

// Debug Counters
volatile uint32_t debug_cs_falling = 0;
volatile uint32_t debug_sclk_total = 0;

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
        strncpy(time_str, data, p1 - data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2 != NULL) {
            strncpy(date_str, p1 + 1, p2 - p1 - 1);
            char* p3 = strchr(p2 + 1, '|');
            if (p3 != NULL) {
                char unread_buf[8] = "";
                strncpy(unread_buf, p2 + 1, p3 - p2 - 1);
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

    // Status bar
    display.setTextSize(2);
    display.setCursor(10, 8);
    display.print("KyPhone");

    // Clock + date vertically centered above button row (y=35 to ~520)
    int total_h = 80 + 24 + 24;
    int start_y = 35 + (475 - total_h) / 2;

    // Clock — textSize 10 = 60px wide per char, 80px tall
    display.setTextSize(10);
    int clock_w = strlen(time_str) * 60;
    display.setCursor((600 - clock_w) / 2, start_y);
    display.print(time_str);

    // Date — textSize 3 = 18px wide per char, 24px tall
    display.setTextSize(3);
    int date_w = strlen(date_str) * 18;
    display.setCursor((600 - date_w) / 2, start_y + 80 + 24);
    display.print(date_str);

    // YAP / CHILL inline separator labels (textSize 1 = 6px wide per char)
    int line_y = 526;
    int pad = 4;

    // YAP spans [0, 292]  (TEXT btn right edge: 150+142)
    {
        int lx = 0, rx = 292;
        int lw = 3 * 6; // "YAP" = 3 chars
        int label_x = lx + (rx - lx - lw) / 2;
        int label_y = line_y - 8;
        display.drawLine(lx, line_y - 4, lx, line_y + 4, BLACK);
        display.drawLine(rx, line_y - 4, rx, line_y + 4, BLACK);
        display.drawLine(lx, line_y, label_x - pad, line_y, BLACK);
        display.drawLine(label_x + lw + pad, line_y, rx, line_y, BLACK);
        display.setTextSize(1);
        display.setCursor(label_x, label_y);
        display.print("YAP");
    }

    // CHILL spans [308, 599]  (READ btn left edge to screen edge)
    {
        int lx = 308, rx = 599;
        int lw = 5 * 6; // "CHILL" = 5 chars
        int label_x = lx + (rx - lx - lw) / 2;
        int label_y = line_y - 8;
        display.drawLine(lx, line_y - 4, lx, line_y + 4, BLACK);
        display.drawLine(rx, line_y - 4, rx, line_y + 4, BLACK);
        display.drawLine(lx, line_y, label_x - pad, line_y, BLACK);
        display.drawLine(label_x + lw + pad, line_y, rx, line_y, BLACK);
        display.setTextSize(1);
        display.setCursor(label_x, label_y);
        display.print("CHILL");
    }

    // 4 buttons: TEXT, CALL, READ, LISTEN
    const char* buttons[] = {"TEXT", "CALL", "READ", "LISTEN"};
    int btn_positions[] = {0, 150, 308, 458};
    int btn_w = 142, btn_h = 65, btn_y = 535;

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
        display.setCursor(lx, ly);
        display.print(buttons[i]);
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

    // TEXT centered (4 chars * 18px = 72px wide)
    display.setTextColor(BLACK);
    display.setCursor((600 - 72) / 2, 10);
    display.print("TEXT");

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
        char preview_buf[24] = "";
        char time_buf[12]    = "";

        char* dot1 = strchr(entry_buf, '\xB7');
        if (dot1 != NULL) {
            strncpy(name_buf, entry_buf, dot1 - entry_buf);
            char* dot2 = strchr(dot1 + 1, '\xB7');
            if (dot2 != NULL) {
                strncpy(preview_buf, dot1 + 1, dot2 - dot1 - 1);
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

        // Name — textSize 3, left
        display.setTextSize(3);
        display.setCursor(margin, y + 8);
        display.print(name_buf);

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
    // data = "Name|Y:body|R:body|..."
    // Y = sent by you (left-aligned), R = received (right-aligned)
    char name_buf[32] = "";
    char* pipe = strchr(data, '|');
    if (pipe != NULL) {
        strncpy(name_buf, data, pipe - data);
        data = pipe + 1;
    } else {
        strncpy(name_buf, data, sizeof(name_buf) - 1);
        data = NULL;
    }

    // Centered name header
    display.setTextSize(3);
    int name_x = (600 - (int)strlen(name_buf) * 18) / 2;
    if (name_x < 10) name_x = 10;
    display.setCursor(name_x, 10);
    display.print(name_buf);
    display.drawLine(0, 46, 600, 46, BLACK);

    int y = 60;
    int line_h = 36;
    while (data != NULL && y < 560) {
        char* next = strchr(data, '|');
        char msg_buf[64] = "";
        if (next != NULL) {
            strncpy(msg_buf, data, next - data);
            data = next + 1;
        } else {
            strncpy(msg_buf, data, sizeof(msg_buf) - 1);
            data = NULL;
        }

        char align = 'R';
        char* body = msg_buf;
        if (strlen(msg_buf) >= 2 && msg_buf[1] == ':') {
            align = msg_buf[0];
            body = msg_buf + 2;
        }

        display.setTextSize(3);
        render_wrapped(body, align == 'R', &y, line_h);
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

void setup() {
    Serial.begin(115200);
    delay(2000); 
    Serial.println("\n--- KYPHONE SPI: V4 (NOISE CANCELLER) ---");

    display.begin();
    display.einkOff(); 
    
    reclaim_spi_pins_for_gpio();

    display.pinModeIO(PIN_HANDSHAKE, OUTPUT);
    display.digitalWriteIO(PIN_HANDSHAKE, LOW);
    
    pinMode(PIN_SCLK, INPUT_PULLDOWN);
    pinMode(PIN_MOSI, INPUT_PULLDOWN);

    attachInterrupt(digitalPinToInterrupt(PIN_SCLK), spi_clock_isr, RISING);
    // CS ISR disabled: noise on Pin 15 resets bit_counter mid-transfer.
    // SCLK-timeout framing handles message boundaries instead.

    display.digitalWriteIO(PIN_HANDSHAKE, HIGH);
    Serial.println(">> SYSTEM READY. Send message now.");
}

void loop() {
    static uint32_t last_debug = 0;
    uint32_t now = millis();
    uint32_t now_us = micros();

    if (now - last_debug > 2000) {
        last_debug = now;
        int cs_val = digitalRead(PIN_CS);
        Serial.printf("DEBUG [%lums]: SCLK_Total: %d | Bits: %d | CS: %d\n",
            millis(), debug_sclk_total, bit_counter, cs_val);
    }

    // Framing: 1500ms silence = end of message (410ms transfer at 5kHz for 256 bytes + margin)
    if (!transfer_complete && bit_counter > 0) {
        if (now_us - last_sclk_time > 1500000) {
            if (bit_counter == TOTAL_BITS) {
                transfer_complete = true;
            } else if (bit_counter > TOTAL_BITS) {
                Serial.printf(">> OVERFLOW: %d bits. Resetting.\n", bit_counter);
                bit_counter = 0;
            } else {
                // Partial chunk — keep accumulating, don't reset.
                // Reset timer so this doesn't re-fire every loop() iteration.
                Serial.printf(">> PARTIAL: %d/%d bits. Waiting for rest...\n", bit_counter, TOTAL_BITS);
                last_sclk_time = micros();
            }
        }
    }

    if (transfer_complete) {
        display.digitalWriteIO(PIN_HANDSHAKE, LOW);
        
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
                    display.clearDisplay();
                    render_home(text + 10);
                    display.partialUpdate();
                    delay(100);
                    display.einkOff();
                    reclaim_spi_pins_for_gpio();
                } else if (strncmp(text, "MSG_LIST_FAST|", 14) == 0) {
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
                        render_home(text + 5);
                    } else if (strncmp(text, "MSG_LIST|", 9) == 0) {
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
                        render_msg_thread(text + 11);
                    } else {
                        render_sms(text);
                    }
                    display.display();
                    delay(100);
                    display.einkOff();
                    reclaim_spi_pins_for_gpio();
                }
            }
        } else {
            Serial.println(">> ERROR: No Header (0x02). Check MOSI wiring.");
            Serial.printf("Raw Data: %02X %02X %02X %02X\n", local_buf[0], local_buf[1], local_buf[2], local_buf[3]);
        }

        display.digitalWriteIO(PIN_HANDSHAKE, HIGH);
    }
}