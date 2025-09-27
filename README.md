# **KyPhone Display Driver**

This repo contains the prototype display driver and UI dev environment for the KyPhone project. This code provides a communication bridge between an Android application running in an emulator and an Inkplate 4 TEMPERA e-ink display, enabling screen mirroring and bidirectional data transfer.

---

## **Current Status (September 25, 2025\)**

### **Milestone 1: Bidirectional MVP Complete**

The initial goal of a stable, bidirectional link is complete. The system now reliably mirrors the Android screen to the hardware and forwards mock touch events from the hardware back to the emulator to trigger live taps.

* **Reliable, Timed Updates:** The full pipeline (App → Proxy → Inkplate) works reliably.  
* **Bidirectional Communication:** The Inkplate sends mock coordinates back to the proxy.  
* **Live Touch Interaction:** The proxy uses `adb` to inject the received coordinates as tap events into the Android emulator, creating a live demo loop.  

### **Next Milestone: Live Touchscreen Integration**

The current system uses randomly generated coordinates from the Inkplate. The next milestone is to integrate the Inkplate's actual touchscreen controller to send real user input to the Android app.

---

## **How It Works**

The system uses a network proxy bridge to connect the Android emulator to the physical hardware.

* **The Android App (Client):** The KyPhone UI runs in the emulator. A `ScreenCaptureService` captures the display and sends it over a network socket.  
* **`adb reverse` (Port Forwarder):** Forwards the network connection from the emulator to the host machine.  
* **`proxy.py` (Server/Bridge):** A Python server on the host machine receives image data from the app and sends it to the Inkplate via serial (UART). It also receives coordinates from the Inkplate and injects them into the emulator as taps.  
* **`inkplate_touch_simulator.ino` (Device Firmware):** An Arduino sketch on the Inkplate listens for serial data, renders the image, and sends mock touch coordinates back.

---

## **Setup & Usage**

The system requires three components to be running simultaneously in the correct order.

### **1\. Configure the Inkplate**

Open `inkplate_touch_simulator/inkplate_touch_simulator.ino` in the Arduino IDE and upload the latest sketch.

### **2\. Prepare the Host Computer (Terminal)**

**Start the Proxy Server:** Navigate to your project folder and run:  
Bash  
python3 ./proxy.py

* 

**Start Port Forwarding:** In a *second terminal*, navigate to your Android SDK's `platform-tools` directory and run:  
Bash  
./adb reverse tcp:65432 tcp:65432

* 

### **3\. Run the App (Android Studio)**

* Open the KyPhone project, start the emulator, and run the app.  
* Tap "Start Screen Mirroring" and grant screen capture permission.

---

## **Development Log**

### **September 25, 2025: Bidirectional MVP Stabilized**

Completed a major debugging effort to achieve a stable, bidirectional link between the Android app and the Inkplate. This involved fixing several layered issues across the entire stack.

* **Firmware:** Resolved a critical out-of-memory crash on the ESP32 by replacing the `drawBitmap()` function with a memory-efficient `drawPixel()` loop. This prevents the device from crashing when receiving the 45KB image payload. Also fixed a subsequent bug that caused garbled screens by ensuring both black and white pixels were drawn.  
* **Proxy Server:** Implemented a buffering mechanism to handle TCP stream chunking, ensuring the full 45KB image is received from the app before being forwarded to the Inkplate.  
* **Protocol:** Implemented a robust request/acknowledgment (ACK) protocol between all three components to fix a series of race conditions, deadlocks, and uncontrolled loops that were preventing reliable, continuous screen updates.  
* **Project Cleanup:** Archived all legacy scripts and firmware from previous development stages into an `_archive` directory to clean up the main project structure.

### **September 14, 2025: Milestone 1 Complete \- Reliable PoC Achieved**

Successfully debugged and stabilized the entire emulator-to-hardware pipeline. The system now reliably mirrors the Android screen to the Inkplate with timed updates.

* **Firmware:** Fixed a critical serial buffer overflow on the ESP32 by implementing a chunked data reading loop in the `.ino` sketch.  
* **Proxy Server:** Resolved network race conditions by implementing an ACK protocol.  
* **Android App:** Fixed multiple issues, including an event-driven screen capture that failed on static screens.  
* **Visuals:** Corrected color inversion and aspect ratio distortion.

### **September 11, 2025: Investigating Continuous Mirroring Bug**

* **Issue:** While the first frame is transmitted and displayed correctly, all subsequent frames fail to appear on the Inkplate hardware.

### **September 10, 2025: Android Prototyping and Emulator-to-Hardware Bridge**

* **Key Milestones Achieved:** Android Dev Environment Setup, Emulator-to-Hardware Bridge Architecture, Live Screen Mirroring (Polling Method).

### **August 30, 2025**

* **Investigated MuditaOS** as an alternative to a custom Linux/Android build.  
* **Decision:** The best path forward is to stick with the Rock 3A and build our custom UI on top of a minimal Linux or AOSP build.

---

## **Appendix: Original Python-Only Demo (Legacy)**

The following documentation describes the initial, legacy milestone of the project. All of these files are now in the `_archive` folder.

* `image_sender.py` (Client/Host): Python script that prepared images and managed communication.  
* `inkplate_receiver.ino` (Server/Device): Arduino sketch for the Inkplate that used `drawBitmap` to render images.
