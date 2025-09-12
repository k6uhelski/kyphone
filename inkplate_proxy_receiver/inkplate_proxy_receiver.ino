#include "Inkplate.h"

Inkplate display(INKPLATE_1BIT);

// Define image constants
const int IMG_WIDTH = 600;
const int IMG_HEIGHT = 600;
const int BUFFER_SIZE = (IMG_WIDTH * IMG_HEIGHT) / 8;

// Global buffer to hold the image
uint8_t imageBuffer[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10000); // Set a timeout for reading data
  display.begin();
  
  display.clearDisplay();
  display.setCursor(10, 10);
  display.setTextSize(3);
  display.print("Proxy Ready.");
  display.display();
}

void loop() {
  // Wait for the START and IMG_DATA headers
  if (Serial.find("START\nIMG_DATA\n")) {
    
    size_t bytesRead = Serial.readBytes(imageBuffer, BUFFER_SIZE);

    display.clearDisplay();
    if (bytesRead == BUFFER_SIZE) {
      // Success: Draw the image
      display.drawBitmap(0, 0, imageBuffer, IMG_WIDTH, IMG_HEIGHT, BLACK);
    } else {
      // Failure: Show an error message
      display.setCursor(10, 10);
      display.setTextSize(2);
      display.print("Data stream error!");
    }
    // This is a slow, blocking call
    display.display();
  }
}

