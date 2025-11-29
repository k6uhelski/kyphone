#include "Inkplate.h"

Inkplate display(INKPLATE_1BIT);

// --- New Global Variables ---
const int IMAGE_WIDTH = 600;
const int IMAGE_HEIGHT = 600;
const int ROW_BYTES = IMAGE_WIDTH / 8; // = 75

int currentByte = 0; 
int currentY = 0;    

// --- MODIFIED: For Gesture Protocol ---
bool isTouching = false; 
uint16_t lastX = 0; // Tracks the last X position
uint16_t lastY = 0; // Tracks the last Y position

void setup() {
  Serial.begin(115200);
  display.begin();

  if (display.tsInit(true)) {
    Serial.println("Touchscreen init OK!");
  } else {
    Serial.println("Touchscreen init fail!");
    while (true); 
  }

  display.clearDisplay();
  display.setTextSize(5);
  display.setCursor(150, 250);
  display.print("KyPhone MVP");
  display.setCursor(100, 350);
  display.print("Waiting for App...");
  display.display();

  Serial.println("Inkplate Ready. Bidirectional communication active.");
}

void handleImageReception() {
  if (Serial.available() > 0) {
    byte pixel_data = Serial.read();

    for (int b = 0; b < 8; b++) {
      int x = (currentByte * 8) + b;
      if (!(pixel_data & (0x80 >> b))) {
        display.drawPixel(x, currentY, BLACK);
      } else {
        display.drawPixel(x, currentY, WHITE);
      }
    }

    currentByte++;

    if (currentByte >= ROW_BYTES) {
      currentByte = 0; 
      currentY++;      
    }

    if (currentY >= IMAGE_HEIGHT) {
      currentY = 0; 
      
      display.display();  
      Serial.println("ACK"); 
    }
  }
}

// --- MODIFIED: This function now sends DOWN, DRAG, and UP ---
void handleTouchPolling() {
  if (display.tsAvailable()) {
    uint8_t n;
    uint16_t x[2], y[2];
    n = display.tsGetData(x, y); 

    // Check if a new touch has *started*
    if (n > 0 && !isTouching) {
      isTouching = true; 
      lastX = x[0];
      lastY = y[0];
      Serial.printf("DOWN:%d,%d\n", lastX, lastY);
    }
    // Check if the touch is *dragging* (like the example sketch)
    else if (n > 0 && isTouching) {
      // Send a DRAG event only if the coordinate has changed
      if (x[0] != lastX || y[0] != lastY) {
        lastX = x[0];
        lastY = y[0];
        Serial.printf("DRAG:%d,%d\n", lastX, lastY);
      }
    }
    // Check if the touch has been *released*
    else if (n == 0 && isTouching) {
      isTouching = false; 
      Serial.println("UP:0,0"); // We don't get coords on release, so send 0,0
    }
  }
}

void loop() {
  handleImageReception();
  handleTouchPolling();
}