# **KyPhone Display Driver**

KyPhone is a minimal phone designed to revolt against the attention economy. It uses a **Radxa Rock 3A** (SBC running Linux) as the logic master and an **Inkplate 4 TEMPERA** (ESP32-based E-ink display) as the peripheral. The goal is a device that gets out of the way of real-world connection.

---

## **Current Status (March 30, 2026)**

KyPhoneOS is running on hardware. The Radxa polls for SMS, renders a full navigation UI on the Inkplate, and persists conversations across reboots. Local development runs on Mac via a pygame simulator — no hardware needed to iterate on UI.

### **What works today**
- **Reliable SPI transport:** Radxa → Inkplate over 3-wire software SPI + handshake. 128-byte payload, 5kHz clock. 1.7s end-to-end latency.
- **Full navigation UI:** HOME → MSG_LIST → MSG_THREAD. Keyboard navigation via evdev (USB keyboard on Radxa).
- **Live SMS via Twilio:** Inbound messages appear on screen in ~2 seconds. Read state and conversation history persisted to disk.
- **Pygame simulator:** Run `python3 spi_bridge/kyphone_app.py --sim` on Mac to develop UI without hardware.
- **Partial clock updates:** Clock refreshes every minute with `partialUpdate()` — no full E-ink flash.

### **Running it**
```
# On Radxa (hardware)
export TWILIO_SID=... TWILIO_TOKEN=... TWILIO_NUMBER=...
python3 spi_bridge/kyphone_app.py

# On Mac (simulator)
pip3 install twilio pygame
export TWILIO_SID=... TWILIO_TOKEN=... TWILIO_NUMBER=...
python3 spi_bridge/kyphone_app.py --sim
```

### **What's next**
- Improve messaging flow (compose, reply, contacts)
- CALL / READ / LISTEN screen placeholders → real implementations
- Physical BlackBerry Q10 keyboard arriving May

---

### **(Previous Milestones)**

### **Milestone 4: Conditional Screen Updates & Full Gestures**

The current system constantly refreshes the e-ink screen every few seconds, even if the Android UI is static. This is inefficient and creates a poor user experience. Our next goal is to make the e-ink display a true **synchronized mirror** of the Android UI, updating *only* when the UI *actually* changes.

* **Change Detection:** The Android app will be updated to compare the current frame to the last-sent frame. If they are identical, no data will be sent.
* **Differential Updates:** When a change *is* detected, the app will calculate a "diff" (the difference) and send *only* the changed pixels.
* **Partial Refresh:** The Inkplate firmware will be updated to receive these partial "diff" commands and draw them directly, allowing for fast, flash-free updates for small UI changes.

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

* **The Android App (Client):** The KyPhone UI runs in the emulator. An `AccessibilityService` performs three tasks:
    1.  **Change Detection:** Captures the display and compares pixels to the previous frame.
    2.  **Transmission:** Sends image data to the Host IP (`10.0.2.2`) only if changes are detected.
    3.  **Input:** Listens for `DOWN`, `DRAG`, and `UP` commands and injects them as system gestures.
* **`proxy.py` (Server/Bridge):** A Python script running on the host computer. It accepts a TCP connection from the Emulator and a Serial connection from the Inkplate, forwarding data between them.
* **`inkplate_touch_simulator.ino` (Device Firmware):** An Arduino sketch running on the Inkplate. It renders received images to the screen and reads touch input, sending coordinate data over the USB Serial connection.

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

### **March 30, 2026: KyPhoneOS — Full SMS App Running on Hardware**

The SPI transport is now a solved problem. This session was entirely product: a full phone OS running on the Radxa, pushing semantic screen commands to the Inkplate over the proven SPI bridge.

**What got built:**
- **KyPhone OS app** (`kyphone_app.py`): Replaces the old test scripts. Polls Twilio every 2 seconds for inbound SMS, manages navigation state, and pushes screen updates to the Inkplate in real time.
- **Navigation state machine:** Three screens — HOME, MSG_LIST, MSG_THREAD — with full keyboard navigation via evdev. Arrow keys + Enter move between screens; messages mark as read when opened.
- **Message persistence:** Conversations saved to `data/messages.json` on disk. Survive reboots. SID tracking prevents replaying old messages on startup.
- **Partial clock updates:** Home screen refreshes the clock every minute using `HOME_FAST` + `partialUpdate()` instead of a full E-ink flash. Noticeably faster.
- **Pygame simulator:** Added `--sim` flag to run the full app locally on Mac — no Inkplate or Radxa needed. A 600x600 pygame window mimics the display layout exactly, dramatically accelerating UI development and iteration.
- **Home screen redesign:** Replaced the single YAP circle button with a 4-button row — TEXT, CALL, READ, LISTEN — grouped under YAP (communication) and CHILL (media) labels. LEFT/RIGHT navigate, ENTER activates.

**Key decisions:**
- Twilio for SMS instead of a modem for now. 10DLC campaign registration submitted for the KyPhone number — pending approval before outbound replies work reliably.
- BlackBerry Q10 keyboard ordered (white BLE+USB, AliExpress, ~$74) for physical input. Arriving May. Until then, USB keyboard or simulator.
- Cellular modem deferred until the device has a battery and enclosure.

---

### **March 23, 2026: SPI Transport Proven — "hello world" on E-ink**

Solved the SPI communication layer completely. Key breakthroughs:

* **IO_MUX reclaim:** `Inkplate.h` calls `SPI.begin()` which reassigns GPIO 13 (MOSI) and GPIO 15 (CS) to the hardware SPI peripheral at the IO_MUX level, bypassing the GPIO Matrix. `attachInterrupt()` is blind to pins in IO_MUX mode. Fix: call `PIN_FUNC_SELECT(IO_MUX_GPIO13_REG, 2)` and `PIN_FUNC_SELECT(IO_MUX_GPIO15_REG, 2)` after `display.begin()` to force both pins back to GPIO mode.
* **CS abandoned:** GPIO 15 is the ESP32 MTDO strapping pin. The display controller PCB traces hold it LOW permanently. Switched to SCLK-timeout framing — 500ms of silence after the last clock edge signals end-of-message.
* **Partial accumulation:** The Rockchip SPI DMA at 5kHz delivers all 272 bits in one 54ms burst, but we accumulated bits across multiple attempts while debugging. Current firmware never resets `bit_counter` on a partial timeout — it just waits for more bits.
* **30-second bottleneck diagnosed:** `timing_baseline.py` revealed the Radxa sends all bits in 54ms, but the firmware waited 30 seconds before evaluating. Reducing the timeout to 500ms dropped end-to-end latency from ~2 minutes to 1.7 seconds.
* **Payload expanded:** 34 → 128 bytes. Enables full SMS message display.

### **March 15, 2026: Phase 1 - The SPI Handshake Nightmare (UNSTABLE)**

Achieved the first *sporadic* text transfers between the Radxa Rock 3A and the Inkplate 4 TEMPERA. While a physical link was established, the system remains **highly unreliable and is currently a "one-hit wonder"**—it typically works for exactly one message and then fails consistently on all subsequent attempts until a hard reset.

**The Current (Unstable) Solution: Synchronous Payload Polling**
We moved away from "Blind Blasting" to a two-way conversation, but it is not yet robust:
*   **Physical Change:** Connected the **Yellow Wire (MISO)** from Radxa Pin 21 to Inkplate IO 12 to enable bidirectional communication.
*   **The ACK Strategy:** The Inkplate pre-loads its SPI outbox with `0x06` (ACK) when ready. The Radxa sends the full 34-byte message as a "poll" and checks the received first byte for the ACK.
*   **Smart Scan:** The Inkplate firmware now scans the entire 34-byte buffer for the `0x02` header to handle shifting offsets.
*   **Sync Logic:** Added `[0x00, 0x00]` dummy bytes to satisfy hardware wake-up lag.

**Current Failure Point:** After the first successful display update, the ESP32 SPI slave consistently fails to re-synchronize after the E-ink refresh. The system is currently stuck in a state where it cannot receive multiple messages in a row.

### **November 28, 2025: Milestone 4 - Conditional Screen Updates & Full Gestures**

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
