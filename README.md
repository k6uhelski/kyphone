# **KyPhone Display Driver**

This repo contains the prototype display driver and UI dev environment for the KyPhone project. This code provides a communication bridge between an Android application running in an emulator and an Inkplate 4 TEMPERA e-ink display, enabling screen mirroring and bidirectional data transfer.

---

## **Current Status (November 12, 2025)**

### **Next Milestone: Synchronized On-Demand Refresh**

The current system constantly refreshes the e-ink screen every few seconds, even if the Android UI is static. This is inefficient and creates a poor user experience. Our next goal is to make the e-ink display a true **synchronized mirror** of the Android UI, updating *only* when the UI *actually* changes.

* **Change Detection:** The Android app will be updated to compare the current frame to the last-sent frame. If they are identical, no data will be sent.
* **Differential Updates:** When a change *is* detected, the app will calculate a "diff" (the difference) and send *only* the changed pixels.
* **Partial Refresh:** The Inkplate firmware will be updated to receive these partial "diff" commands and draw them directly, allowing for fast, flash-free updates for small UI changes.

---

### **(Previous Milestones)**

### **Milestone 3: Live Touchscreen Integration Complete**

The goal of a stable, live-touch bidirectional link is complete. The system now reliably mirrors the Android screen to the hardware and forwards **real** user taps from the Inkplate's touchscreen controller back to the emulator to trigger live taps.

* **Live Touch:** The Inkplate firmware is now non-blocking and sends real `TAP:X,Y` coordinates from the touchscreen controller.
* **App-Side Parsing:** The Android `AccessibilityService` correctly parses the `TAP:X,Y` protocol and injects live tap events.
* **`adb`-Free:** The `adb reverse` dependency has been eliminated by updating the app to connect directly to the host IP (`10.0.2.2`).

### **Milestone 2: Tap Injection Moved to Android App**

The architecture has been refactored to remove the `adb` dependency. All tap injection logic, which was previously handled by the Python proxy, has been moved into the Android application itself.

* **No More `adb`:** The `proxy.py` script no longer uses `subprocess` to call `adb`. It is now a simple data bridge that forwards image data and coordinate strings.
* **Accessibility Service:** The `ScreenCaptureService` has been refactored into an `AccessibilityService`.
* **Live App-Side Taps:** The service now listens for coordinates from the proxy and uses `dispatchGesture()` to inject system-wide taps directly, completing the new control loop (`Inkplate` → `Proxy` → `App` → `Android System`).

### **Milestone 1: Bidirectional MVP Complete**

The initial goal of a stable, bidirectional link is complete. The system now reliably mirrors the Android screen to the hardware and forwards mock touch events from the hardware back to the emulator to trigger live taps.

* **Reliable, Timed Updates:** The full pipeline (App → Proxy → Inkplate) works reliably.
* **Bidirectional Communication:** The Inkplate sends mock coordinates back to the proxy.
* **Live Touch Interaction:** The proxy uses `adb` to inject the received coordinates as tap events into the Android emulator, creating a live demo loop.

---

## **How It Works**

The system uses a network proxy bridge to connect the Android emulator to the physical hardware.

* **The Android App (Client):** The KyPhone UI runs in the emulator. An `AccessibilityService` captures the display, sends it over a network socket (to `127.0.0.1`), and listens on the same socket for incoming touch coordinates. When coordinates are received, it injects them as system-wide taps.
* **`adb reverse` (Port Forwarder):** Forwards the network connection from the emulator's `127.0.0.1` to the host machine's `127.0.0.1`.
* **`proxy.py` (Server/Bridge):** A Python server on the host machine (listening on `127.0.0.1`) receives image data from the app and sends it to the Inkplate via serial (UART). It also receives coordinates from the Inkplate and forwards them to the app.
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
* One-Time Setup: Go to Settings > Accessibility > KyPhone and enable the "Use KyPhone" toggle. You must grant the permission for the service to inject taps.
* Tap "Start Screen Mirroring" and grant screen capture permission.

---

## **Development Log**

### **November 12, 2025: Milestone 3 - Live Touchscreen Integration**

Successfully integrated the Inkplate's real touchscreen controller, completing the emulator-to-hardware loop. Replaced the mock coordinate system with a robust, non-blocking hardware loop.

* **Firmware:** Upgraded `inkplate_touch_simulator.ino` to be fully non-blocking. It now handles simultaneous image receiving and touch polling, capturing all tap events without dropping input.
* **Android App:** Updated `ScreenCaptureService.kt` to parse the new `TAP:X,Y` string protocol, removing the parsing crash.

### **November 10, 2025: Milestone 2 - Tap Injection Refactored to Android**

Successfully refactored the architecture to move all tap injection logic from the Python proxy into the Android app. This removes the `adb` dependency and is a major step toward the final on-device software.

* **Proxy Server:** Removed `adb` and `subprocess` logic. The proxy is now a simple, fast data bridge.
* **Android App:** Refactored `ScreenCaptureService` into an `AccessibilityService`. The service now manages the socket connection (sending images and receiving coordinates) and uses `dispatchGesture()` to inject system-wide taps.

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
