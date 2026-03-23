# Design: SPI-Repeatability

## 1. Executive Summary

| Item | Details |
|------|---------|
| Feature | SPI-Repeatability |
| Objective | Formalize 3-Wire SPI + Dedicated Handshake Pin architecture. |
| Status | Design Phase (Approved) |
| Match Rate | 0% (Initial Design) |

### Value Delivered
| Problem | Solution | Function UX Effect | Core Value |
|---------|----------|-------------------|------------|
| Kernel EBUSY on SPI MISO pin. | Move handshake to dedicated GPIO (Pin 15). | Unblocked, reliable handshake signal. | Hardware/Software decoupling. |

## 2. System Architecture

### 2.1 Hardware Pin Mapping
| Function | Radxa Pin | Inkplate Pin | Wire Color | Notes |
|----------|-----------|--------------|------------|-------|
| MOSI | 19 | IO 13 | Purple | Shared |
| SCLK | 23 | IO 14 | White | Shared |
| CS | 24 | IO 15 | Blue | Shared |
| **HANDSHAKE** | **15** | **IO 12** | **Yellow** | **Dedicated GPIO** |
| GND | 6 | GND | Grey | Common |

### 2.2 Communication Protocol (Handshake-Locked SPI)
1.  **Inkplate Busy**: Sets IO 12 to `INPUT_PULLDOWN` or `LOW`.
2.  **Inkplate Ready**: Sets IO 12 to `OUTPUT` and `HIGH`.
3.  **Radxa Loop**:
    *   Polls Pin 15 (GPIO3_C4).
    *   If `HIGH`: Sends SPI payload (34 bytes).
    *   If `LOW`: Waits and retries.

## 3. Implementation Plan

### 3.1 Inkplate Firmware (`Inkplate_SPI_Peripheral.ino`)
*   `setupSPI()`: Releases Master SPI, resets pins to `INPUT`, initializes `spi_slave`, then pulls IO 12 `HIGH`.
*   `loop()`: On success, pulls IO 12 `LOW`, switches to Master SPI, draws to screen, then calls `setupSPI()`.

### 3.2 Radxa Script (`string_test.py`)
*   Initialize `gpiod` on Chip 3, Line 20 (Pin 15).
*   Wait for Line 20 to be `1` before calling `spi.xfer2()`.

## 4. Verification Plan
1.  **Handshake Test**: Confirm Pin 15 goes HIGH after Inkplate boot.
2.  **SPI Test**: Confirm message appears on E-ink display.
3.  **Repeatability Test**: Send 5 messages in 10 seconds.
