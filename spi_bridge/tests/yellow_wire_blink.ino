#include <Inkplate.h>

Inkplate display(INKPLATE_1BIT);

// AGREED HANDSHAKE: Physical Pin P1-0 is IO_PIN_B0
#define PIN_YELLOW IO_PIN_B0 

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- STARTING P1-0 EXPANDER BLINK TEST ---");
    
    display.begin();
    display.pinModeIO(PIN_YELLOW, OUTPUT);
}

void loop() {
    Serial.println("Driving P1-0 HIGH (Should read 1 on Radxa)");
    display.digitalWriteIO(PIN_YELLOW, HIGH);
    delay(2000);
    
    Serial.println("Driving P1-0 LOW (Should read 0 on Radxa)");
    display.digitalWriteIO(PIN_YELLOW, LOW);
    delay(2000);
}
