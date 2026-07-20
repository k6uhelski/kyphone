#include <Inkplate.h>

Inkplate display(INKPLATE_1BIT);

// Test each pin on each expander looking for the handshake (P1-0 = IO_PIN_B0 = 8)
// Watch serial output — the correct pin will show HIGH after we set it,
// and the Radxa handshake line will also go HIGH when we find the right one.

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("--- HANDSHAKE PIN FINDER ---");
    display.begin();

    // Try expander2, pin 8 (IO_PIN_B0)
    Serial.println("Testing expander2, pin 8...");
    display.expander2.pinMode(8, OUTPUT, true);
    display.expander2.digitalWrite(8, LOW, true);
    delay(500);
    Serial.println("Set LOW. Check Radxa: should read 0");
    delay(2000);

    display.expander2.digitalWrite(8, HIGH, true);
    Serial.println("Set HIGH. Check Radxa: should read 1");
    delay(2000);

    // Also try expander1 pin 8 just in case
    Serial.println("Testing expander1, pin 8...");
    display.expander1.pinMode(8, OUTPUT, true);
    display.expander1.digitalWrite(8, LOW, true);
    delay(500);
    Serial.println("Set LOW.");
    delay(2000);
    display.expander1.digitalWrite(8, HIGH, true);
    Serial.println("Set HIGH.");
    delay(2000);

    Serial.println("Done. Check which expander made the Radxa see a change.");
}

void loop() {}
