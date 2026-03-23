# Plan: SPI-Repeatability

## 1. Executive Summary

| Item | Details |
|------|---------|
| Feature | SPI-Repeatability |
| Objective | Achieve consistently repeatable 4-wire SPI communication between Radxa and Inkplate using shared pins. |
| Status | In Progress (Phase 2.5: Clean Toggling) |
| Match Rate | 0% (Initial Plan) |

### Value Delivered
| Problem | Solution | Function UX Effect | Core Value |
|---------|----------|-------------------|------------|
| E-ink display refresh causes "Dead Zone" in SPI communication. | Implement hardware handshake and pin state toggling. | Instant resume of communication after display update. | System reliability and responsiveness. |

## 2. Objective & Scope
Achieve consistently repeatable SPI communication between the Radxa (Master) and Inkplate (Slave) using the current shared pins (13, 14, 15). The goal is to fix the software state reset after an E-ink display refresh.

## 3. Current Status & Diagnostic
- [x] **Diagnose Failure State:** Confirmed Radxa timeouts and Inkplate "Dead Zone" post-draw.
- [x] **Revert to VSPI_HOST + Forced Resets:** Partial success, but "Dead Zone" persists.

## 4. Technical Approach
- **Phase 2.5: Clean Toggling & Handshake (CURRENT)**
    - Explicitly release pins on Inkplate after display refresh (`pinMode(INPUT)`).
    - Implement physical handshake signal on a dedicated GPIO (Radxa Pin 15 / Inkplate IO 12).
    - Update Radxa script to wait for "Ready" signal.
- **Phase 3: Hardware Alternative (BACKUP)**
    - Move SPI communication to dedicated, unused GPIO pins (GPIO 25, 26, 27) on the Inkplate header.

## 5. Success Criteria
1.  **Repeatability:** Radxa can send 5+ consecutive messages with display updates without timing out.
2.  **Responsiveness:** Communication resumes immediately (< 500ms) after a display refresh finishes.
3.  **Reliability:** No garbage data received after state resets.

## 6. Risk Assessment
- **Hardware Conflict:** Display controller may physically clamp pins. (Mitigated by Phase 3 pivot).
- **Kernel Lock:** Radxa SPI driver may prevent GPIO access on shared pins. (Mitigated by moving handshake to Pin 15).
