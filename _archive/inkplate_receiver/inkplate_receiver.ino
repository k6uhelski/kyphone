#include "Inkplate.h"

Inkplate display(INKPLATE_1BIT);

// (Constants and global variables are the same)
const int IMG_WIDTH = 600;
const int IMG_HEIGHT = 600;
const int BUFFER_SIZE = (IMG_WIDTH * IMG_HEIGHT) / 8;
uint8_t imageBuffer[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(5000);
  display.begin();
  
  display.clearDisplay();
  display.setCursor(10, 10);
  display.setTextSize(3);
  display.print("KyPhone Ready.");
  display.display();
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "START") {
      Serial.println("READY");

      while (true) {
        command = Serial.readStringUntil('\n');
        command.trim();

        if (command == "IMG_DATA") {
          handleFullUpdate();
        } else if (command.startsWith("PARTIAL_DATA")) {
          int x, y, w, h;
          sscanf(command.c_str(), "PARTIAL_DATA,%d,%d,%d,%d", &x, &y, &w, &h);
          handlePartialUpdate(x, y, w, h);
        } else if (command == "") {
          break;
        }
      }
    }
  }
  delay(100);
}

void handleFullUpdate() {
  Serial.println("READY_FOR_IMG");
  size_t totalBytesRead = receiveData(imageBuffer, BUFFER_SIZE);

  display.clearDisplay();
  if (totalBytesRead == BUFFER_SIZE) {
    display.drawBitmap(0, 0, imageBuffer, IMG_WIDTH, IMG_HEIGHT, BLACK);
  } else {
    display.setCursor(10, 10);
    display.setTextSize(2);
    display.print("Full update error!");
  }
  display.display();
}

void handlePartialUpdate(int x, int y, int w, int h) {
  Serial.println("READY_FOR_PARTIAL");
  
  int partialBufferSize = (w * h) / 8;
  uint8_t* partialBuffer = new uint8_t[partialBufferSize];
  
  size_t totalBytesRead = receiveData(partialBuffer, partialBufferSize);

  if (totalBytesRead == partialBufferSize) {
    // Draw the small bitmap to the correct coordinates in the main buffer
    display.drawBitmap(x, y, partialBuffer, w, h, BLACK);
    // --- THIS IS THE FIX ---
    // Tell the screen to refresh ONLY that small rectangular area
    display.partialUpdate(); 
  }
  
  delete[] partialBuffer;
}

// (The receiveData function is unchanged)
size_t receiveData(uint8_t* buffer, size_t bufferSize) {
  const int CHUNK_SIZE = 4096;
  size_t totalBytesRead = 0;
  while (totalBytesRead < bufferSize) {
    size_t remaining = bufferSize - totalBytesRead;
    size_t toRead = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    size_t bytesRead = Serial.readBytes(buffer + totalBytesRead, toRead);
    
    if (bytesRead == toRead) {
      totalBytesRead += bytesRead;
      Serial.println("ACK");
    } else {
      break; 
    }
  }
  return totalBytesRead;
}