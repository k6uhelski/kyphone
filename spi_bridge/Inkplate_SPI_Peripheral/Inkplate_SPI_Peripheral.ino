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
#define PAYLOAD_BYTES 128
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
    // data = "HH:MM|Day, Mon DD|N new messages"
    char time_str[16] = "";
    char date_str[32] = "";
    char notif_str[32] = "";

    char* p1 = strchr(data, '|');
    if (p1 != NULL) {
        strncpy(time_str, data, p1 - data);
        char* p2 = strchr(p1 + 1, '|');
        if (p2 != NULL) {
            strncpy(date_str, p1 + 1, p2 - p1 - 1);
            strncpy(notif_str, p2 + 1, sizeof(notif_str) - 1);
        } else {
            strncpy(date_str, p1 + 1, sizeof(date_str) - 1);
        }
    } else {
        strncpy(time_str, data, sizeof(time_str) - 1);
    }

    // Status bar placeholder
    display.setTextSize(2);
    display.setCursor(10, 8);
    display.print("KyPhone");

    // Divider under status bar
    display.drawLine(0, 34, 600, 34, BLACK);

    // Clock — large, centered
    display.setTextSize(10);
    int clock_x = 30;
    display.setCursor(clock_x, 60);
    display.print(time_str);

    // Date — medium, centered
    display.setTextSize(3);
    int date_w = strlen(date_str) * 18;
    display.setCursor((600 - date_w) / 2, 200);
    display.print(date_str);

    // Divider
    display.drawLine(0, 260, 600, 260, BLACK);

    // Notification summary
    display.setTextSize(3);
    display.setCursor(20, 290);
    display.print(notif_str);
}

void render_msg_list(char* data, int selected) {
    // data = "Name1·preview1|Name2·preview2|..."
    // selected = index of highlighted row
    display.setTextSize(3);
    display.setCursor(20, 10);
    display.print("Messages");
    display.drawLine(0, 46, 600, 46, BLACK);

    int y = 60;
    int row = 0;
    char* entry = data;
    while (entry != NULL && y < 560) {
        char* next = strchr(entry, '|');
        char entry_buf[64] = "";
        if (next != NULL) {
            strncpy(entry_buf, entry, next - entry);
            entry = next + 1;
        } else {
            strncpy(entry_buf, entry, sizeof(entry_buf) - 1);
            entry = NULL;
        }

        bool is_selected = (row == selected);
        if (is_selected) {
            display.fillRect(0, y - 4, 600, 84, BLACK);
            display.setTextColor(WHITE);
        }

        // Split entry on middle dot (0xB7 raw)
        char* dot = strchr(entry_buf, '\xB7');
        if (dot != NULL) {
            *dot = '\0';
            display.setTextSize(3);
            display.setCursor(20, y);
            display.print(entry_buf);
            display.setTextSize(2);
            display.setCursor(20, y + 34);
            display.print(dot + 1);
        } else {
            display.setTextSize(3);
            display.setCursor(20, y);
            display.print(entry_buf);
        }

        if (is_selected) {
            display.setTextColor(BLACK);
        }

        row++;
        y += 90;
        display.drawLine(0, y - 6, 600, y - 6, BLACK);
    }
}

void render_msg_thread(char* data) {
    // data = "Name|msg1|msg2|..."
    char name_buf[32] = "";
    char* pipe = strchr(data, '|');
    if (pipe != NULL) {
        strncpy(name_buf, data, pipe - data);
        data = pipe + 1;
    } else {
        strncpy(name_buf, data, sizeof(name_buf) - 1);
        data = NULL;
    }

    // Header: sender name
    display.setTextSize(3);
    display.setCursor(20, 10);
    display.print(name_buf);
    display.drawLine(0, 46, 600, 46, BLACK);

    int y = 60;
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
        display.setTextSize(2);
        display.setCursor(20, y);
        display.print(msg_buf);
        y += 30;
    }
}

void render_clock_tick(char* time_str) {
    // Erase only the clock region and redraw, then partial update
    display.fillRect(0, 55, 600, 110, WHITE);
    display.setTextSize(10);
    display.setCursor(30, 60);
    display.print(time_str);
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

    // Framing: 500ms silence = end of message (54ms transfer at 5kHz + 10x margin)
    if (!transfer_complete && bit_counter > 0) {
        if (now_us - last_sclk_time > 500000) {
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

                if (strncmp(text, "HOME_TICK|", 10) == 0) {
                    // Partial update — do NOT clearDisplay, do NOT call display()
                    render_clock_tick(text + 10);
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