# KyPhone Hardware Wiring Guide

This document maintains the absolute source of truth for the physical wiring between the Radxa Rock 3A and the Inkplate 4 TEMPERA.

## Current Setup: 3-Wire + Expander Handshake (Final Agreement)

*Orientation: Radxa 40-pin header on the right, USB ports facing down. Pin 1 and 2 at the top.*
*Inside Column = Left column (odd pins). Outside Column = Right column (even pins).*

| Radxa Location | Radxa Pin # | Wire Color | Function | Connects To | Inkplate Label | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Inside Col, 7th down | **Pin 13** | **Yellow** | **Handshake** | ➔ | **P1-0** | **Expander Pin (Left Header)** |
| Inside Col, 10th down | **Pin 19** | **Purple** | MOSI (Data Out)| ➔ | **IO 13** | Shared (Native) |
| Inside Col, 12th down | **Pin 23** | **White** | CLK (Clock) | ➔ | **IO 14** | Shared (Native) |
| Outside Col, 12th down| **Pin 24** | **Blue** | CS0 (Chip Select)| ➔ | **IO 15** | Shared (Native) |
| Outside Col, 3rd down | **Pin 6** | **Grey** | GND (Ground) | ➔ | **GND** | Required for common ground |

*Note: This setup uses Radxa Pin 13 (Line 21) to poll the Inkplate's P1-0 expander pin. This is the only physical configuration confirmed to carry a 3.3V signal across the boards.*
