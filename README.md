# KyPhone Display Driver

This repo contains the prototype display driver and UI dev environment for the KyPhone project.
This code provides a communication bridge between an Android application running in an emulator and an Inkplate 4 TEMPERA eInk display.

## Milestone 1 (Completed)

Establish a proof-of-concept (PoC) whereby the Android UI running in an emulator is reliably mirrored on the physical Inkplate display, with the screen updating at consistent, timed intervals.

## Next Milestone: AOSP Integration

The current POC relies on inefficient polling (timed loop in an app). The next milestone is to integrate the display logic directly into the Android Open Source Project (AOSP) for an event-driven approach.

This will make the Inkplate behave like a native display, updating only when the screen content actually changes. I think this scope is:
* Setting up an AOSP build environment.
* Hooking into Android's native display pipeline (SurfaceFlinger) to capture screen updates as they occur.
* Re-implementing the data transfer logic in C++ at the system level, preparing for the move to the final Rock 3A hardware and moving to the faster SPI communication protocol.

## How the POC works

The system uses a network proxy bridge to connect the Android emulator to the physical hardware, bypassing the emulator's USB restrictions.
* **The Android App (Client):** The KyPhone UI runs as a native app in the Android emulator. A `ScreenCaptureService` captures the live display, processes it for the display, and sends it over a network socket.
* **adb reverse (Port Forwarder):** This standard Android tool forwards the network connection from the emulator's `localhost` to the host machine's `localhost`.
* **proxy.py (Server/Bridge):** A Python server runs on the host machine, listening for the forwarded connection. It receives image data from the Android app and sends it to the Inkplate over a serial (UART) connection.
* **inkplate_proxy_receiver.ino (Device Firmware):** An Arduino sketch on the Inkplate's ESP32 listens for serial data from the proxy and renders the received image.

## Hardware

* Inkplate 4 TEMPERA
* Host computer (macOS/Linux/Windows)
* USB-C data cable

## Software setup

### 1. Configure the Inkplate

Open `inkplate_proxy_receiver/inkplate_proxy_receiver.ino` in the Arduino IDE.
Upload the sketch to the Inkplate.
*Note:* You may need to manually enter bootloader mode for the upload to succeed. To do this, press and hold the I/O button, press and release the WAKE button, then release I/O. Click "Upload" immediately after.

### 2. Configure the host computer

Install and configure Android Studio, the Android SDK, and a virtual device.
Ensure Python 3 is installed.
Install the required Python libraries from your terminal:
`pip3 install pyserial Pillow`

## Usage

The system requires three components to be running simultaneously.

1.  **Start the bridge (Terminal #1):** In a terminal, navigate to project folder and run the Python proxy server.
    ```
    python3 ./proxy.py
    ```
2.  **Start the port forwarder (Terminal #2):** In a second terminal, navigate to your Android SDK's `platform-tools` directory and run the `adb reverse` command.
    ```
    ./adb reverse tcp:65432 tcp:65432
    ```
3.  **Run the KyPhone app (Android Studio):** Open the KyPhone project in Android Studio, start the emulator, and run the app. Tap the "Start Screen Mirroring" button and grant the screen capture permission. The emulator's screen will now appear on the physical Inkplate display.

## Current Status (September 2025)

### Milestone 1: Proof-of-Concept Complete

The initial goal of reliably mirroring the emulator screen to the hardware through polling (3s) is complete. The system is now stable and includes fixes to previous reliability issues.

- [x] **Reliable, timed updates:** The full pipeline (App → Proxy → Inkplate) works reliably, updating the display at a consistent interval.
- [x] **Stable proxy server:** The Python server is robust and handles client connections and disconnects without crashing (most of the time :) ).
- [x] **Robust firmware:** The Inkplate firmware correctly receives the entire 45,000-byte image payload without buffer overflows.

---

## Development Log

### September 14, 2025: Milestone 1 Complete - Reliable PoC Achieved
Successfully debugged and stabilized the entire emulator-to-hardware pipeline. The system now reliably mirrors the Android screen to the Inkplate with timed updates. This effort involved a deep dive into the full application stack to identify and resolve multiple issues.
* **Firmware:** Fixed a critical serial buffer overflow on the ESP32 by implementing a chunked data reading loop in the `.ino` sketch.
* **Proxy Server:** Resolved network race conditions that caused transfers to fail after the first frame by implementing a request/acknowledgment (ACK) protocol between the Python server and the Android client.
* **Android App:** Fixed multiple issues, including an event-driven screen capture that failed on static screens, and stubborn deployment problems that caused old builds to run on the emulator. The final app now forces updates by re-transmitting the last known frame if a new one is not available.
* **Visuals:** Corrected color inversion in the firmware and fixed aspect ratio distortion by implementing a center-crop in the app's image processing logic.

### September 11, 2025: Investigating Continuous Mirroring Bug

* **Issue:** While the first frame is transmitted and displayed correctly, all subsequent frames sent by the Android app fail to appear on the Inkplate hardware.
* **Current theory:** The initial handshake and data transfer works, but there may be an issue in the firmware's main loop that prevents it from correctly receiving or processing data packets after the first image. The Python proxy appears to be sending the data correctly.
* **Next step:** Add detailed serial logging to both the Python proxy and the `inkplate_proxy_receiver.ino` firmware to trace the data flow for the second frame and identify the point of failure.

### September 10, 2025: Android Prototyping and Emulator-to-Hardware Bridge

Began the next phase of development by moving from a simple Python-based host to a more robust Android application environment. The goal is to develop the KyPhone's UI as a native Android app running in an emulator, while having it display on the physical Inkplate hardware.
* **Key Milestones Achieved:**
    * Android Development Environment Setup: Successfully installed and configured Android Studio, the Android SDK, and a virtual device (Pixel 6) for emulation.
    * Emulator-to-Hardware Bridge Architecture: Designed and implemented a network proxy bridge to bypass the emulator's inability to directly access host USB devices.
    * Live Screen Mirroring (Polling Method): Implemented a `ScreenCaptureService` within the Android app using the `MediaProjection` API to capture and transmit the live screen of the emulator.

### August 30, 2025

Investigated MuditaOS as an alternative to a custom Linux/Android build, as it's a minimal OS purpose built for E Ink displays.
* Assuming I'm understanding correctly, their docs show MuditaOS is built specifically for the NXP RT1051 microcontroller, which is a different architecture from our Rockchip RK3568. A direct port would require a significant, low-level driver development effort.
* Considered switching to RT1051 microcontroller, but it's powerful enough to run the required apps (e.g., Spotify, Google Maps).
* **Decision:** The best path forward is to stick with the Rock 3A and build our custom UI on top of a minimal Linux or AOSP build. However, switching to the RT1051 and MuditaOS remains an option to consider in the future.

---

## Appendix: Original Python-Only Demo (Legacy)

The following documentation describes the initial milestone of the project. This version used a direct Python script-to-hardware connection and does not involve Android.

### How It Worked

The system used a Client/Server model over a serial (UART) connection:
* `image_sender.py` (Client/Host): A Python script that prepared 600x600 1-bit images and managed the communication protocol. It ran on the host computer.
* `inkplate_receiver.ino` (Server/Device): An Arduino sketch for the Inkplate's onboard ESP32. It listened for commands and used the `drawBitmap` function to render the received image data. (Note: A different sketch than inkplate_proxy_receiver.ino)

### Software Setup

* **Configure the Inkplate**
    * Open `inkplate_receiver/inkplate_receiver.ino` in the Arduino IDE and upload it.
* **Configure the Host Computer**
    * Install the required Python libraries:
        ```
        pip3 install pyserial Pillow pynput
        ```
    * **macOS Specific Setup:** To allow the script to listen for keyboard input, grant permissions in `System Settings > Privacy & Security > Input Monitoring` for your terminal and/or code editor. The script also required admin privileges.

### Usage

The `image_sender.py` script was an interactive UI demo that listened for keyboard presses to change the displayed screen.
* Navigate to the project's root directory in your terminal.
* Run the script with `sudo`:
    ```
    sudo python3 image_sender.py
    ```
* Press `1`, `2`, or `3` to switch between the different UI screens. Press `q` to quit.
