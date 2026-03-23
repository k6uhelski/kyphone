# KyPhone SPI Handoff Context
*Date: March 21, 2026*

## 1. Goal
Establish reliable, unidirectional 3-wire SPI + 1-wire Handshake communication from a Radxa Rock 3A (Master) to an Inkplate 4 TEMPERA (ESP32 Slave).

## 2. Hardware Constraints & Current Wiring
We *must* use the shared display pins. We cannot solder or move the SPI data wires.
*   **MOSI:** Radxa Pin 19 ➔ Inkplate **IO 13**
*   **SCLK:** Radxa Pin 23 ➔ Inkplate **IO 14**
*   **CS:** Radxa Pin 24 ➔ Inkplate **IO 15**
*   **Handshake (Ready Signal):** Inkplate **P1-0** (Expander `IO_PIN_B0`) ➔ Radxa **Pin 13** (`gpiochip3`, Line 21)

## 3. The Journey (Why Hardware SPI Failed)
1.  We attempted standard ESP-IDF `spi_slave` on HSPI (13/14/15). It failed (Radxa sent data, ESP32 received nothing).
2.  We ran a "Naked Test" (removed Inkplate display library entirely). It still failed.
3.  **Diagnosis:** The E-ink display controller is physically hardwired to 13/14/15. Its presence causes severe signal degradation/ringing. The hardware SPI edge detectors reject the signal as corrupted.
4.  **Proof of Signal:** A simple `digitalRead()` loop in a previous diagnostic sketch successfully counted exactly 272 clock pulses (34 bytes) on these pins, proving the voltage arrives, but requires a "dumb/slow" reader to act as a low-pass filter against the ringing.

## 4. Current Architecture: V3 Software SPI
Because hardware SPI is impossible on these pins, we pivoted to **Path B: Custom Interrupt-Driven Software SPI**.
*   **Radxa Script (`spi_bridge/string_test.py`):** Runs at 5kHz. Waits for Handshake to go HIGH, sends 34 bytes (272 bits), then sleeps 500ms to avoid race conditions. This is working perfectly.
*   **Inkplate Firmware (`spi_bridge/Inkplate_SPI_Peripheral.ino`):** Uses IRAM-allocated ISRs. 
    *   `CS` is set to `GPIO_INTR_ANYEDGE`. Falling edge zeroes the volatile buffer and resets the bit counter.
    *   `SCLK` is set to `GPIO_INTR_POSEDGE`. Reads MOSI via `GPIO.in` and shifts bits into the buffer.

## 5. The Current Bug
The Radxa successfully reads the handshake and transmits the data. However, the Inkplate serial monitor stops at:
`>> V3 SYSTEM READY: WAITING FOR RADXA...`

**The interrupts are either not firing, firing but immediately aborting, or stuck in a state where `transfer_complete` never triggers.**

## 6. Next Steps for New Session
1.  Analyze the ISR logic in `Inkplate_SPI_Peripheral.ino`.
2.  Add lightweight debugging (e.g., a simple flag or counter) to the `cs_edge_isr` to verify if the CS falling edge is actually being detected by the ESP32 hardware interrupt system.
3.  Ensure the ESP32 GPIO matrix is correctly routing external signals to the interrupt controller when standard `pinMode()` is used.