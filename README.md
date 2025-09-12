# KyPhone Display Driver
This repo contains the prototype display driver and UI dev environment for the KyPhone project.

This code provides a communication bridge between an Android application running in an emulator and an Inkplate 4 TEMPERA eInk display.

## Goal
PoC whereby the Android UI is displayed on the Inkplate at _n_ second intervals. 

## Next stage
- Once established the next step is to update the AOSP directly so that it can render the display on the inkplate when the screen changes (i.e. a button is pressed). 
- Take the finished, Android application and all of its display logic and deploy it to the final Rock 3A hardware. Re-implement the communication bridge using the much faster SPI protocol for responsive, production-level experience.

## How it works
The system uses a network proxy bridge to connect the Android emulator to the physical hardware, bypassing the emulator's USB restrictions (blocks ports).

* **The Android App (Client):** The KyPhone UI runs as a native app in the Android emulator. A `ScreenCaptureService` captures the live display, processes it for the display, and sends it over a network socket.
* **`adb reverse` (Port Forwarder):** This standard Android tool forwards the network connection from the emulator's `localhost` to the host machine's `localhost`.
* **`proxy.py` (Server/Bridge):** A Python server runs on the host machine, listening for the forwarded connection. It receives image data from the Android app and sends it to the Inkplate over a serial (UART) connection.
* **`inkplate_proxy_receiver.ino` (Device Firmware):** An Arduino sketch on the Inkplate's ESP32 listens for serial data from the proxy and renders the received image.

## Hardware
* Inkplate 4 TEMPERA
* Host computer (macOS/Linux/Windows)
* USB-C data cable

## Software setup
### 1. Configure the Inkplate
* Open `inkplate_proxy_receiver/inkplate_proxy_receiver.ino` in the Arduino IDE.
* Upload the sketch to the Inkplate.
* **Note:** You may need to manually enter bootloader mode for the upload to succeed. To do this, press and **hold the `I/O` button**, press and **release the `WAKE` button**, then **release `I/O`**. Click "Upload" immediately after.

### 2. Configure the host computer
* Install and configure **Android Studio**, the Android SDK, and a virtual device.
* Ensure Python 3 is installed.
* Install the required Python libraries from your terminal:
    ```bash
    pip3 install pyserial Pillow
    ```

## Usage
The system requires three components to be running simultaneously.

1.  **Start the bridge (Terminal #1):** In a terminal, navigate to project folder and run Python proxy server. It will connect to the Inkplate and wait for a connection from the app.
    ```bash
    python3 proxy.py
    ```

2.  **Start the port forwarder (Terminal #2):** In a second terminal, navigate to your Android SDK's `platform-tools` directory and run the `adb reverse` command.
    ```bash
    ./adb reverse tcp:65432 tcp:65432
    ```

3.  **Run the KyPhone app (Android Studio):** Open the KyPhone project in Android Studio, start the emulator, and run the app. Tap the "Start Screen Mirroring" button and grant the screen capture permission. The emulator's screen will now appear on the physical Inkplate display (though it's buggy).

## Current Status (September 2025)
The initial connection in the emulator-to-hardware bridge is functional, but a bug is preventing the goal of continuous updates (every 3 seconds or so).

* **Functionality:** The system successfully captures the first frame from the Android emulator and displays it on the physical Inkplate screen.
* **Known Bug:** The screen mirroring only updates once when the app is static. I think it's because the acquireLatestImage() function only provides a frame when the screen content changes. However, even when Android logs showed that images were being transfered to the Inkplate, it very rarely updated more than once. 
* **Next steps: (Where I need Scott's help)** Debug the data transfer protocol between the Python proxy and the Inkplate firmware to resolve the issue with subsequent frames such that it reliably sends/displays images on the Inkplate either (i) at 5-10s intervals and/or (ii) when the screen is updated (displays somethng else)

## Appendix: Original Python-Only Demo (Legacy)
The following documentation describes the initial milestone of the project. This version used a direct Python script-to-hardware connection and does **not** involve Android.

### How It Worked
The system used a Client/Server model over a serial (UART) connection:
* **`image_sender.py` (Client/Host):** A Python script that prepared 600x600 1-bit images and managed the communication protocol. It ran on the host computer.
* **`inkplate_receiver.ino` (Server/Device):** An Arduino sketch for the Inkplate's onboard ESP32. It listened for commands and used the `drawBitmap` function to render the received image data. (Note: A different sketch than inkplate_proxy_receiver.ino)

### Software Setup
1.  **Configure the Inkplate**
    * Open `inkplate_receiver/inkplate_receiver.ino` in the Arduino IDE and upload it.

2.  **Configure the Host Computer**
    * Install the required Python libraries:
        ```bash
        pip3 install pyserial Pillow pynput
        ```
    * **macOS Specific Setup:** To allow the script to listen for keyboard input, grant permissions in **System Settings > Privacy & Security > Input Monitoring** for your terminal and/or code editor. The script also required admin privileges.

### Usage
The `image_sender.py` script was an interactive UI demo that listened for keyboard presses to change the displayed screen.
1.  Navigate to the project's root directory in your terminal.
2.  Run the script with `sudo`:
    ```bash
    sudo python3 image_sender.py
    ```
3.  Press `1`, `2`, or `3` to switch between the different UI screens. Press `q` to quit.

## Development Log

### September 11, 2025: Investigating Continuous Mirroring Bug
* **Issue:** While the first frame is transmitted and displayed correctly, all subsequent frames sent by the Android app fail to appear on the Inkplate hardware.
* **Current theory:** The initial handshake and data transfer works, but there may be an issue in the firmware's main loop that prevents it from correctly receiving or processing data packets after the first image. The Python proxy appears to be sending the data correctly.
* **Next step:** Add detailed serial logging to both the Python proxy and the `inkplate_proxy_receiver.ino` firmware to trace the data flow for the second frame and identify the point of failure.

### September 10, 2025: Android Prototyping and Emulator-to-Hardware Bridge
Began the next phase of development by moving from a simple Python-based host to a more robust Android application environment. The goal is to develop the KyPhone's UI as a native Android app running in an emulator, while having it display on the physical Inkplate hardware.

* **Key Milestones Achieved:**
    * **Android Development Environment Setup:** Successfully installed and configured Android Studio, the Android SDK, and a virtual device (Pixel 6) for emulation.
    * **Emulator-to-Hardware Bridge Architecture:** Designed and implemented a network proxy bridge to bypass the emulator's inability to directly access host USB devices.
    * **Live Screen Mirroring (Polling Method):** Implemented a `ScreenCaptureService` within the Android app using the `MediaProjection` API to capture and transmit the live screen of the emulator.

### August 30, 2025
* Investigated MuditaOS as an alternative to a custom Linux/Android build, as it's a minimal OS puprose built for E Ink displays.
* Assuming I'm understanding correctly, their docs show MuditaOS is built specifically for the [NXP RT1051](https://github.com/mudita/MuditaOS/blob/master/doc/build_targets.md) microcontroller, which is a different architecture from our Rockchip RK3568. A direct port would require a significant, low-level driver development effort.
* Considered switching to RT1051 microcontroller, but it's powerful enough to run the required apps (e.g., Spotify, Google Maps). 
* **Decision:** The best path forward is to stick with the Rock 3A and build our custom UI on top of a minimal Linux or AOSP build. However, switching to the RT1051 and MuditaOS remains an option to consider in the future.
