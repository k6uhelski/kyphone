#include "Inkplate.h"

// Initialize in 1-bit (black and white) color mode.
Inkplate display(INKPLATE_1BIT);

const int IMAGE_SIZE = 45000;
byte imageBuffer[IMAGE_SIZE];

// A counter to keep track of how many bytes we've received so far.
int bytesReceived = 0;

void setup() {
  Serial.begin(115200);
  display.begin();
  display.clearDisplay();

  display.setCursor(20, 250);
  display.setTextSize(5);
  display.print("Ready.");
  display.display();
  
  Serial.println("Inkplate is ready.");
}

void loop() {
  // Check if there are any bytes available to read.
  if (Serial.available() > 0) {
    
    // Read all available bytes (up to the remaining buffer space).
    int bytesToRead = Serial.available();
    if (bytesToRead > (IMAGE_SIZE - bytesReceived)) {
      bytesToRead = IMAGE_SIZE - bytesReceived;
    }
    
    // Read the incoming bytes into the correct position in our large buffer.
    Serial.readBytes(imageBuffer + bytesReceived, bytesToRead);
    bytesReceived += bytesToRead;
    
    Serial.print("Received ");
    Serial.print(bytesReceived);
    Serial.println(" / 45000 bytes...");

    // Check if we have now received the full image.
    if (bytesReceived >= IMAGE_SIZE) {
      Serial.println("Full image received! Drawing now...");
      
      display.clearDisplay();
      
      // THE FIX IS HERE: Swapped BLACK and WHITE to invert the image
      display.drawBitmap(0, 0, imageBuffer, 600, 600, WHITE, BLACK);
      
      display.display();
      
      Serial.println("Screen updated successfully!");
      
      // IMPORTANT: Reset the counter to be ready for the next image.
      bytesReceived = 0;
    }
  }
}