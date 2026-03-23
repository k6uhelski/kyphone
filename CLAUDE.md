# CLAUDE.md - KyPhone Project Technical Handoff

## 1. Project Overview
**KyPhone** is a project to create an E-ink "phone" interface by using a **Radxa Rock 3A** (Single Board Computer) as the logic master and an **Inkplate 4 TEMPERA** (ESP32-based E-ink display) as the synchronized display peripheral.

*   **Final Goal:** Unidirectional data transmission from Radxa to Inkplate to mirror a UI or display system notifications with low power consumption and high sunlight readability.
*   **System Architecture:** 
    *   **Master (Radxa):** Runs a Python-based SPI controller. It manages the application logic and "pushes" display updates.
    *   **Slave (Inkplate):** Runs a custom interrupt-driven software SPI peripheral. It listens for incoming bitstreams and renders them to the E-ink panel.
    *   **Handshake:** A 1-wire "Ready" signal from the Slave to the Master prevents buffer overflows and ensures the Slave is not busy with a display refresh.
    *   **Latency Requirements:** Must be sufficient for smooth UI navigation (text updates), though E-ink refresh rates are the ultimate bottleneck.

## 2. Hardware Inventory
*   **Master:** Radxa Rock 3A (Rockchip RK3568, Debian 11).
*   **Slave:** Inkplate 4 TEMPERA (ESP32-WROVER-E, 8MB PSRAM).
*   **OS/Firmware:**
    *   **Radxa:** Linux 5.10 kernel, `gpiod` v1.6.
    *   **Inkplate:** Arduino Core for ESP32 (v2.x or v3.x), `Inkplate.h` library.

### **Pin Assignment & Wiring**
| Function | Radxa Pin | Wire Color | `gpiod` / `ESP32` Label | Role |
| :--- | :--- | :--- | :--- | :--- |
| **MOSI** | Pin 19 | 🟣 Purple | `gpiochip3`, Line 9 / **IO 13** | Data |
| **SCLK** | Pin 23 | ⚪ White | `gpiochip3`, Line 8 / **IO 14** | Clock |
| **CS (SS)** | Pin 24 | 🔵 Blue | `gpiochip3`, Line 10 / **IO 15** | Framing |
| **Handshake**| Pin 13 | 🟡 Yellow | `gpiochip3`, Line 21 / **P1-0** | Flow Control |
| **GND** | Pin 6 | 🔘 Grey | Common Ground | Stability |

### **PCB-Level Constraints**
*   **Shared Nets:** Pins 13, 14, and 15 are physically hardwired to the on-board E-ink peripheral controller. 
*   **Strapping Pin:** **GPIO 15** is the ESP32 `MTDO` strapping pin. It has an internal pull-down at boot to select flash voltage/mode.
*   **Signal Integrity:** The display controller adds significant impedance and ringing to these traces, making hardware SPI peripherals fail at the silicon logic level.

## 3. Communication Layer — Full History
1.  **Hardware SPI (Failed):** Attempted standard ESP-IDF `spi_slave`. The hardware SPI peripheral rejected the noisy signals arriving on the shared display pins. No data was captured.
2.  **Naked SPI Test (Failed):** Removed the `Inkplate.h` library to eliminate software interference. Hardware SPI still failed, proving the display controller hardware itself is the source of the signal degradation.
3.  **Polling Software SPI (Partially Successful):** Used a `while(digitalRead(SCLK))` loop. Proved that voltage is reaching the pins and counted exactly 272 pulses. Abandoned because polling blocks the CPU and misses edges when the display is active.
4.  **V3 Interrupt-Driven SPI (Current):** Switched to ISRs on `SCLK` (posedge) and `CS` (anyedge).
    *   **Issue:** `SCLK` interrupts fire perfectly (272 counts), but `CS` interrupts remain at 0.

## 4. Current State of the Code

### **Slave: `spi_bridge/Inkplate_SPI_Peripheral.ino` (V4)**
Uses a hybrid approach: **IO_MUX Recapture** + **SCLK Timeout Fallback**.
```cpp
#include <Inkplate.h>
#include <driver/gpio.h>
#include "soc/io_mux_reg.h"

Inkplate display(INKPLATE_1BIT); 
#define PIN_MOSI 13
#define PIN_SCLK 14
#define PIN_CS   15
#define PIN_HANDSHAKE IO_PIN_B0
#define TOTAL_BITS 272

volatile uint8_t rx_buf[34];
volatile uint16_t bit_counter = 0;
volatile bool transfer_complete = false;
volatile uint32_t last_sclk_time = 0;
volatile uint32_t last_cs_time = 0;

void reclaim_pin15_for_gpio() {
    PIN_FUNC_SELECT(IO_MUX_GPIO15_REG, 2); // Force to GPIO function
    gpio_set_pull_mode((gpio_num_t)PIN_CS, GPIO_PULLUP_ONLY); // Fight strapping pull-down
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_CS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE, 
    };
    gpio_config(&io_conf);
}

void IRAM_ATTR spi_clock_isr() {
    uint32_t now = micros();
    last_sclk_time = now;
    if (bit_counter < TOTAL_BITS) {
        bool mosi_high = (GPIO.in >> PIN_MOSI) & 0x1;
        uint8_t byte_idx = bit_counter / 8;
        uint8_t bit_idx = 7 - (bit_counter % 8); 
        if (mosi_high) rx_buf[byte_idx] |= (1 << bit_idx);
        else           rx_buf[byte_idx] &= ~(1 << bit_idx);
        bit_counter++;
    }
}

void IRAM_ATTR cs_falling_isr() {
    uint32_t now = micros();
    if (now - last_cs_time > 1000) { // 1ms Debounce
        bit_counter = 0;
        transfer_complete = false;
    }
    last_cs_time = now;
}

void setup() {
    Serial.begin(115200);
    display.begin();
    display.einkOff(); 
    reclaim_pin15_for_gpio();
    pinMode(PIN_SCLK, INPUT_PULLDOWN);
    pinMode(PIN_MOSI, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(PIN_SCLK), spi_clock_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_CS), cs_falling_isr, FALLING);
    display.digitalWriteIO(PIN_HANDSHAKE, HIGH);
}

void loop() {
    if (!transfer_complete && bit_counter > 0) {
        if (micros() - last_sclk_time > 50000) { // 50ms silence = Framing
            if (bit_counter == TOTAL_BITS) transfer_complete = true;
            else bit_counter = 0;
        }
    }
    if (transfer_complete) {
        // ... Process local_buf, update display, then re-call reclaim_pin15_for_gpio()
    }
}
```

### **Master: `spi_bridge/string_test.py`**
```python
import spidev
import gpiod
import time

spi = spidev.SpiDev()
spi.open(3, 0)
spi.max_speed_hz = 5000 # Slow for noise tolerance
spi.mode = 0

chip = gpiod.Chip('gpiochip3')
line = chip.get_line(21) # Yellow wire
line.request(consumer='kyphone', type=gpiod.LINE_REQ_DIR_IN)

def send_message(text):
    payload = [0x00, 0x00, 0x02] + [ord(c) for c in text[:30]]
    payload += [0x00] * (34 - len(payload))
    while int(line.get_value()) == 0: time.sleep(0.1)
    spi.xfer2(payload)
    time.sleep(0.5) 
```

## 5. The Pin 15 Problem — Everything You Know
*   **Symptoms:** `SCLK` interrupts fire 272 times correctly. `CS` (Pin 15) interrupts fire 0 times, or fire hundreds of times (noise) but never a clean framing pulse.
*   **Hypothesis 1 (IO_MUX):** The `Inkplate.h` library calls `SPI.begin()`, which sets `IO_MUX_GPIO15_REG` to Function 1 (SPI CS0). This routes the pin directly to the SPI peripheral, bypassing the GPIO Matrix required for `attachInterrupt()`. **Status: Tested/Confirmed fix requires recapture.**
*   **Hypothesis 2 (Strapping Pin):** GPIO 15 is `MTDO`. It has a boot-time pull-down. On the Inkplate PCB, it might be heavily clamped to 0V or 3.3V to ensure boot stability, making it difficult for the Radxa to pull it low enough to trigger an edge. **Status: Mitigating with internal PULLUP.**
*   **Hypothesis 3 (Library Theft):** The Inkplate library re-initializes the display peripheral during `display.display()` or `display.einkOff()`, silently re-claiming Pin 15 for the SPI hardware.

## 6. Your Current Trajectory: CS-less SCLK-Timeout
*   **Logic:** Since `SCLK` is the only reliable signal, we use it for both data clocking AND framing.
*   **Framing Protocol:** 
    1.  Master pulls Handshake HIGH when ready.
    2.  Slave resets `bit_counter` on the first `SCLK` edge (or first CS falling edge if available).
    3.  Slave counts exactly 272 bits.
    4.  **Timeout:** If `SCLK` is silent for `> 50ms`, the Slave evaluates the `bit_counter`. If it is exactly 272, the message is "Finalized" and `transfer_complete` is set.
*   **Status:** Complete in V4 firmware.
*   **Risks:** A single noise pulse on `SCLK` shifts the entire bitstream.

## 7. What Has NOT Been Tried
*   **Hardware Filtering:** Adding a small capacitor to Pin 15 to damp ringing.
*   **Level Shifting:** Confirming if the Radxa (3.3V) and Inkplate (3.3V) have a voltage delta causing the "Invisible 0" on Pin 15.
*   **ESP-IDF GPIO Driver Only:** Completely bypassing the Arduino `attachInterrupt` and `pinMode` layers to use low-level `gpio_isr_handler_add` after manual `io_mux` configuration.

## 8. Open Questions
*   Why does `SCLK` succeed at the IO_MUX level when Pin 15 fails, even though both are used by the E-ink controller? (Current theory: SCLK is an output from the ESP32 to the display, leaving its input-sense matrix path open).
*   Does `display.einkOff()` fully release the SPI bus or just cut power to the panel?

## 9. File & Directory Map
*   `spi_bridge/Inkplate_SPI_Peripheral.ino`: Current Slave firmware (V4).
*   `spi_bridge/string_test.py`: Current Master test script.
*   `spi_bridge/tests/Signal_Detector.ino`: Diagnostic tool for raw pin counting.
*   `spi_bridge/tests/wire_verifier.py`: Toggles all pins slowly to verify physical continuity.
*   `planning/SPI_Handoff_Context.md`: Summary of hardware architecture.
## 10. Product Inspiration & Philosophy
KyPhone is more than a technical exercise; it is a revolt against the attention economy.

*   **The Problem:** Smartphones are designed to keep us connected to people who *aren't* around, often at the expense of those who *are*. They are purveyors of "social time" that cannibalize real-world presence.
*   **The Vision:** A "Minimal Phone" (not just a "Dumb Phone"). 
    *   **Minimalist but expressive:** Moving away from the sterile black/white brick design.
    *   **Artist Collaborations:** Limited editions (e.g., Tombolo) to make the device a statement piece.
    *   **Physical Intent:** Features like a dedicated button to signal if you're "open for a chat."
    *   **Goal:** A device that gets out of the way of your life, using limitations to spark creativity and real connection.
*   **Current Priority:** Transition from technical bottlenecks to a "Proof of Life" demo: **displaying a basic image pushed from the Radxa to the E-ink screen.**
