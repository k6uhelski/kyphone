# KyPhone Project Backlog
*Created: March 3, 2026*

## Phase 1: Hardware Bridge (Current Focus)
- [ ] **Verify SPI communication** between Radxa and Inkplate (No-Handshake Mode).
- [ ] **Implement Hardware Handshake** (RTS Pin) if polling fails or hangs.
- [ ] **Resolve GPIO conflicts** with Touch Controller/SD Card on Pins 13/14.
- [ ] **Stabilize Power**: Ensure common ground and clean power delivery to both boards.

## Phase 2: Display & Input Optimization
- [ ] **Touch Calibration**: Map Inkplate raw coordinates to Radxa screen pixels.
- [ ] **Display Scaling**: Refine 600x600 image processing for centering and clarity.
- [ ] **Differential Updates**: Implement "diffing" to send only changed pixels over SPI.
- [ ] **Partial Refresh**: Update Inkplate firmware to draw only modified regions (flicker-free).

## Phase 3: OS & UI Framework
- [ ] **OS Selection**: Finalize between AOSP (Android) or a minimal Linux build (Radxa).
- [ ] **UI Rendering**: Choose a framework (Qt, Flutter, or Custom Python/C++).
- [ ] **Input Driver**: Write a kernel-level or user-space driver to treat SPI touch as a standard input device.

## Phase 4: Core Phone Functionality
- [ ] **Modem Integration**: Connect LTE/5G module to Radxa via USB or Serial.
- [ ] **Telephony Stack**: Implement Call/SMS handling logic.
- [ ] **Audio Path**: Integrate I2S/USB audio for speaker and microphone.

## Phase 5: Final Assembly
- [ ] **Power Management**: Integrate LiPo battery, charging circuit, and fuel gauge.
- [ ] **Enclosure**: 3D design and print a handheld chassis for all components.
