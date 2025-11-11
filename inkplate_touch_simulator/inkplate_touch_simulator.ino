#include "Inkplate.h"

Inkplate display(INKPLATE_1BIT);

const int IMAGE_WIDTH = 600;
const int IMAGE_HEIGHT = 600;

bool mirroringActive = false;

void setup() {
  Serial.begin(115200);
  display.begin();
  
  display.clearDisplay();
  display.setTextSize(5);
  display.setCursor(150, 250);
  display.print("KyPhone MVP");
  display.setCursor(100, 350);
  display.print("Waiting for App...");
  display.display();

  Serial.println("Inkplate Ready. Bidirectional communication active.");
}

void loop() {
  // Check if the app has started sending an image
  if (Serial.available() > 0 && !mirroringActive) {
    mirroringActive = true; 
    
    for (int y = 0; y < IMAGE_HEIGHT; y++) {
      for (int x_byte = 0; x_byte < (IMAGE_WIDTH / 8); x_byte++) {
        while (Serial.available() == 0) {
          delay(1);
        }
        byte pixel_data = Serial.read();
        for (int b = 0; b < 8; b++) {
          int x = x_byte * 8 + b;
          // Draw both black and white pixels to prevent garbled screen
          if (!(pixel_data & (0x80 >> b))) {
            display.drawPixel(x, y, BLACK);
          } else {
            display.drawPixel(x, y, WHITE);
          }
        }
      }
    }
    
    display.display();

    Serial.println("ACK");
    int x_coord = random(0, 600);
    int y_coord = random(0, 600);
    Serial.printf("%d,%d\n", x_coord, y_coord);

    mirroringActive = false;
  }
}