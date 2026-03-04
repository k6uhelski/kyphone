#include <Inkplate.h>

Inkplate display(INKPLATE_1BIT); 

void setup() {
    Serial.begin(115200);
    display.begin();
    
    // Physical reset and clear
    display.clean(0, 2); 
    display.clearDisplay();

    // Set up the "Ready" message
    display.setTextSize(5);
    display.setTextColor(BLACK);
    display.setCursor(100, 300);
    display.print("KYPHONE READY");

    // Force a full hardware draw
    display.display(); 
    
    Serial.println("If you don't see text now, something is wrong with the display init.");
}

void loop() {
    // Just sit here for now. No SPI.
}