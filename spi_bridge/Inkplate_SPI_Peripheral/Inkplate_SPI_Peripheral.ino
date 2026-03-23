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
#define PAYLOAD_BYTES 34
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
        Serial.printf("DEBUG: CS_VAL: %d | Falling: %d | SCLK_Total: %d | Bits: %d\n", 
            cs_val, debug_cs_falling, debug_sclk_total, bit_counter);
    }

    // Framing: 30s diagnostic timeout — proves bits all arrive eventually (software SPI hypothesis)
    if (!transfer_complete && bit_counter > 0) {
        if (now_us - last_sclk_time > 30000000) {
            if (bit_counter == TOTAL_BITS) {
                transfer_complete = true;
            } else if (bit_counter > TOTAL_BITS) {
                Serial.printf(">> OVERFLOW: %d bits. Resetting.\n", bit_counter);
                bit_counter = 0;
            } else {
                // Partial chunk — keep accumulating, don't reset.
                // Remaining bits will land at correct buffer positions.
                Serial.printf(">> PARTIAL: %d/272 bits. Waiting for rest...\n", bit_counter);
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
            if(local_buf[i] == 0x02) { offset = i; break; }
        }

        if (offset != -1) {
            Serial.printf("SUCCESS! MSG: %s\n", (char*)&local_buf[offset+1]);
            display.clearDisplay();
            display.setTextSize(4);
            display.setCursor(50, 200);
            display.print((char*)&local_buf[offset+1]);
            display.display();
            delay(100);
            display.einkOff();
            reclaim_spi_pins_for_gpio();
        } else {
            Serial.println(">> ERROR: No Header (0x02). Check MOSI wiring.");
            Serial.printf("Raw Data: %02X %02X %02X %02X\n", local_buf[0], local_buf[1], local_buf[2], local_buf[3]);
        }

        display.digitalWriteIO(PIN_HANDSHAKE, HIGH);
    }
}