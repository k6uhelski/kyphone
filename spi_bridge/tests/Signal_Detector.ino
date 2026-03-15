#include <Arduino.h>

// Inkplate IO Pins we are testing
const int PIN_MOSI = 13;
const int PIN_SCLK = 14;
const int PIN_CS   = 15;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Set all three SPI pins as basic inputs
    pinMode(PIN_MOSI, INPUT);
    pinMode(PIN_SCLK, INPUT);
    pinMode(PIN_CS, INPUT);

    Serial.println("--- INKPLATE SIGNAL DETECTOR ---");
    Serial.println("Waiting for signal changes on IO 13, 14, or 15...");
    Serial.println("Run 'string_test.py' on the Radxa and send a message.");
}

// Variables to track the last known state
int last_mosi = -1;
int last_sclk = -1;
int last_cs   = -1;

void loop() {
    int current_mosi = digitalRead(PIN_MOSI);
    int current_sclk = digitalRead(PIN_SCLK);
    int current_cs   = digitalRead(PIN_CS);

    // If any pin changes state, print it out immediately
    if (current_mosi != last_mosi) {
        Serial.print("DETECTED: MOSI (IO 13) changed to ");
        Serial.println(current_mosi);
        last_mosi = current_mosi;
    }
    
    if (current_sclk != last_sclk) {
        Serial.print("DETECTED: SCLK (IO 14) changed to ");
        Serial.println(current_sclk);
        last_sclk = current_sclk;
    }
    
    if (current_cs != last_cs) {
        Serial.print("DETECTED: CS (IO 15) changed to ");
        Serial.println(current_cs);
        last_cs = current_cs;
    }

    // Small delay to prevent flooding the serial monitor
    // We might miss high-speed toggles, but we will catch *some* activity
    delay(1); 
}
