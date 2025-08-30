# KyPhone Display Driver
This repository contains the prototype display driver for the KyPhone project.

This code provides a communication bridge between a host processor (currently a computer, eventually a Rockchip SoC) and an Inkplate 4 TEMPERA e-paper display.

## How It Works
The system uses a Client/Server model over a serial (UART) connection:

* **`image_sender.py` (Client/Host):** A Python script that prepares 600x600 1-bit images and manages the communication protocol. It runs on the host computer.

* **`inkplate_receiver.ino` (Server/Device):** An Arduino sketch for the Inkplate's onboard ESP32. It listens for commands and uses the `drawBitmap` function to render the received image data.

The communication protocol supports both full-screen refreshes and fast partial updates for specific screen regions, using a chunked, acknowledged data transfer for reliability.

## Hardware
* Inkplate 4 TEMPERA
* Host computer (macOS/Linux/Windows)
* USB-C data cable

## Software Setup
### 1. Configure the Inkplate
* Open `inkplate_receiver/inkplate_receiver.ino` in the Arduino IDE.
* Upload the sketch to the Inkplate.
* **Note:** You may need to manually enter bootloader mode for the upload to succeed *(this took me too long to figure out)*. To do this, press and **hold the `I/O` button**, press and **release the `WAKE` button**, then **release `I/O`**. Click "Upload" immediately after.

### 2. Configure the Host Computer
* Ensure Python 3 is installed.
* Install the required Python libraries from your terminal:
    ```bash
    pip3 install pyserial Pillow pynput
    ```
* **macOS Specific Setup:**
    * To allow the script to listen for keyboard input, you must grant permissions. Go to **System Settings > Privacy & Security > Input Monitoring** and add your terminal application (e.g., `Terminal.app`, `iTerm.app`) and/or your code editor (e.g., `VS Code`) to the list.
    * The script must be run with admin privileges.

## Usage
The `image_sender.py` script is an interactive UI demo that listens for keyboard presses to change the displayed screen.

1.  Navigate to the project's root directory in your terminal.
2.  Run the script with `sudo`:
    ```bash
    sudo python3 image_sender.py
    ```
3.  The script will display the initial `intro_screen.png`. You can then press `1`, `2`, or `3` to switch between the different UI screens in the `assets` folder. Press `q` to quit.

## Current Status (August 2025)
The foundational display driver is functional.

* **Protocol:** Serial (UART) communication protocol has been established.
* **Features:** The driver supports both full-screen refreshes and fast, partial updates.
* **Next Steps:** The immediate plan is to continue developing the UI logic on an Android emulator on the host computer. The long-term goal is to port this driver to the Rock 3A SoC and migrate the communication protocol from UART to the much faster SPI for production-level performance. Kyle is traveling and doesn't have his SPI/USB connector on him :).

## Development Log

### August 30, 2025
* Investigated MuditaOS as an alternative to a custom Linux/Android build, as it's a minimal OS puprose built for E Ink displays.
* Assuming I'm understanding correctly, their docs show MuditaOS is built specifically for the [NXP RT1051](https://github.com/mudita/MuditaOS/blob/master/doc/build_targets.md) microcontroller, which is a different architecture from our Rockchip RK3568. A direct port would require a significant, low-level driver development effort.
* Considered switching to RT1051 microcontroller, but it's powerful enough to run the required apps (e.g., Spotify, Google Maps). 
* **Decision:** The best path forward is to stick with the Rock 3A and build our custom UI on top of a minimal Linux or AOSP build. However, switching to the RT1051 and MuditaOS remains an option to consider in the future.