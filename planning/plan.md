# Plan: Fixing SPI Repeatability (Shared Pins)

## Objective
Achieve consistently repeatable 4-wire SPI communication between the Radxa (Master) and Inkplate (Slave) using the current shared pins (13, 14, 15). The first message works, proving the physical link; the goal is to fix the software state reset after an E-ink display refresh.

## Phase 1: Diagnose the Failure State (COMPLETED)
- [x] **1.1 Deploy Diagnostic Code:** Uploaded firmware with "Garbage Printing" and Radxa script.
- [x] **1.2 Trigger the Failure:** Ran "test 1" (success) and "test 2" (failure).
- [x] **1.3 Analyze the Logs:** 
    - **Result:** Radxa times out. Inkplate shows `00` garbage when idle, but goes into a "Dead Zone" (no output) immediately after drawing the first message.
    - **Conclusion:** The E-ink library is leaving the shared pins in a strongly driven output state (causing the Dead Zone). Once they finally float, they pick up electrical noise (causing the `00` garbage).

## Phase 2: Software State Fixes (IN PROGRESS)
We attempted to forcefully decouple the pins from the E-ink library and hand them back to the SPI slave.
- [x] **2.1 Switch to HSPI_HOST:** Failed. Broke the physical link entirely.
- [x] **2.2 Force Pin State Reset & Pull-ups:** Failed. Pull-ups broke the physical link entirely.
- [x] **2.5 Revert to VSPI_HOST + Forced Resets:** Partial Failure. The system reverted to the "Working Once" state, but the "Dead Zone" between the first draw and the return of the `00`s was found to be exceptionally long.

## Phase 2.5: The "Clean Toggling" Strategy (NEW)
Research shows that the ESP-IDF `spi_slave` driver and the Arduino `SPI` Master driver (used by Inkplate) cannot share pins unless they are explicitly killed and restarted.
- [ ] **2.5.1 Explicit Master Kill:** Call `SPI.end()` immediately after the Inkplate finishes drawing to forcefully kill the Arduino Master driver and release the pins.
- [ ] **2.5.2 Explicit Master Start:** Call `SPI.begin()` right before drawing to the screen.

## Phase 3: Hardware Alternative (Option A/B)
Since software multiplexing is a dead end, we must move the communication off the shared display pins.
- [ ] **3.1 Select New Approach:**
    *   **Option A (UART):** Ditch SPI. Connect Radxa TX/RX to dedicated Inkplate GPIOs and reuse the proven Milestone 1 serial code.
    *   **Option B (Dedicated SPI):** Keep SPI, but move the Radxa's MOSI/MISO/SCLK/CS wires to completely unused GPIO pins on the Inkplate header (e.g., 25, 26, 27). The SPI slave will stay permanently initialized.