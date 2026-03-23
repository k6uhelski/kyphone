#include <Inkplate.h>

Inkplate display(INKPLATE_1BIT);

#define PIN_MOSI 13
#define PIN_SCLK 14
#define PIN_CS   15
#define PIN_HANDSHAKE IO_PIN_B0 // P1-0

void setup() {
    Serial.begin(115200);
    
    // Initialize Inkplate & Handshake
    display.begin();
    display.pinModeIO(PIN_HANDSHAKE, OUTPUT);
    
    pinMode(PIN_MOSI, INPUT);
    pinMode(PIN_SCLK, INPUT);
    pinMode(PIN_CS,   INPUT);
    
    delay(2000);
    Serial.println("\n--- INKPLATE SIGNAL DETECTOR (HANDSHAKE READY) ---");
    Serial.println("Watching MOSI(13), SCLK(14), CS(15).");
    
    // SIGNAL READY TO RADXA
    display.digitalWriteIO(PIN_HANDSHAKE, HIGH);
    Serial.println("Handshake HIGH. Send a message from Radxa now...");
}

void loop() {
    static int last_cs = -1;
    int cs = digitalRead(PIN_CS);
    
    // 1. Monitor Chip Select (The "Start" signal)
    if (cs != last_cs) {
        Serial.printf("CS (Pin 15) is now: %s\n", cs == HIGH ? "HIGH (Idle)" : "LOW (ACTIVE)");
        last_cs = cs;
    }
    
    // 2. If Radxa is talking (CS is LOW), count the clock pulses
    if (cs == LOW) {
        long pulses = 0;
        int mosi_high_count = 0;
        unsigned long start_time = millis();
        
        // Sample for 100ms
        while(millis() - start_time < 100) {
            if (digitalRead(PIN_SCLK) == HIGH) {
                pulses++;
                if (digitalRead(PIN_MOSI) == HIGH) mosi_high_count++;
                while(digitalRead(PIN_SCLK) == HIGH && (millis() - start_time < 100)); // Wait for fall
            }
        }
        
        if (pulses > 0) {
            Serial.printf(">> SUCCESS: Received %ld bits. Data seen on MOSI: %s\n", 
                          pulses, mosi_high_count > 0 ? "YES" : "NONE (All Zeros)");
        }
    }
}
