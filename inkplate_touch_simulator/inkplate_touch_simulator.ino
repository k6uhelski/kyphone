#include "Inkplate.h"

Inkplate display(INKPLATE_1BIT);

// --- New Global Variables ---
// To track our position as we draw the image non-blockingly
const int IMAGE_WIDTH = 600;
const int IMAGE_HEIGHT = 600;
const int ROW_BYTES = IMAGE_WIDTH / 8; // = 75

int currentByte = 0; // Tracks which byte in the row (0 to 74)
int currentY = 0;    // Tracks which row (0 to 599)

// --- Touch State Variable ---
bool isTouching = false; 

void setup() {
  Serial.begin(115200);
  display.begin();

  // Init touchscreen
  if (display.tsInit(true)) {
    Serial.println("Touchscreen init OK!");
  } else {
    Serial.println("Touchscreen init fail!");
    while (true); // Halt on failure
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

// --- This is our new, non-blocking image handler ---
void handleImageReception() {
  // Check if there is data to read
  if (Serial.available() > 0) {
    
    // Read just ONE byte of pixel data
    byte pixel_data = Serial.read();

    // Draw the 8 pixels for this byte
    for (int b = 0; b < 8; b++) {
      int x = (currentByte * 8) + b;
      if (!(pixel_data & (0x80 >> b))) {
        display.drawPixel(x, currentY, BLACK);
      } else {
        display.drawPixel(x, currentY, WHITE);
      }
    }

    // Move to the next byte position
    currentByte++;

    // If we've finished a full row...
    if (currentByte >= ROW_BYTES) {
      currentByte = 0; // Reset byte counter
      currentY++;      // Move to the next row
    }

    // If we've finished the entire image...
    if (currentY >= IMAGE_HEIGHT) {
      currentY = 0; // Reset for next image
      
      display.display();  // Refresh the e-ink screen
      Serial.println("ACK"); // Send ACK *after* image is all received
    }
  }
}

// --- This is our non-blocking touch handler (unchanged) ---
void handleTouchPolling() {
  if (display.tsAvailable()) {
    uint8_t n;
    uint16_t x[2], y[2];
    n = display.tsGetData(x, y); 

    // Check if a new touch has *started*
    if (n > 0 && !isTouching) {
      isTouching = true; 
      Serial.printf("TAP:%d,%d\n", x[0], y[0]);
    }
    // Check if the touch has been *released*
    else if (n == 0 && isTouching) {
      isTouching = false; 
      Serial.println("REL:0,0");
    }
  }
}


void loop() {
  // Run both handlers on every single loop.
  // This is non-blocking. Both tasks are handled rapidly.
  handleImageReception();
  handleTouchPolling();
}