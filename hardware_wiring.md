# KyPhone Hardware Wiring Guide

This document maintains the absolute source of truth for the physical wiring between the Radxa Rock 3A and the Inkplate 4 TEMPERA.

## Current Setup: 4-Wire "Handshake" SPI (Safe Mode)

*Orientation: Radxa 40-pin header on the right, USB ports facing down. Pin 1 and 2 at the top.*
*Inside Column = Left column (odd pins). Outside Column = Right column (even pins).*

| Radxa Location | Radxa Pin # | Wire Color | SPI Function | Connects To | Inkplate Label | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Inside Col, 10th down | **Pin 19** | **Purple** | MOSI (Data Out)| ➔ | **IO 13** | Shared with E-Ink Bus |
| Inside Col, 11th down | **Pin 21** | **Yellow** | MISO (Data In) | ➔ | **IO 12** | **"Safe" Return Path (Buzzer)** |
| Inside Col, 12th down | **Pin 23** | **White** | CLK (Clock) | ➔ | **IO 14** | Shared with E-Ink Bus |
| Outside Col, 12th down| **Pin 24** | **Blue** | CS0 (Chip Select)| ➔ | **IO 15** | Shared with E-Ink Bus |
| Outside Col, 3rd down | **Pin 6** | **Grey** | GND (Ground) | ➔ | **GND** | Required for common ground |

*Note: This setup uses a software lock to release IO 13, 14, and 15 before drawing to the E-ink screen to prevent hardware conflicts.*
