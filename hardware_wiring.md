# KyPhone Hardware Wiring Guide

This document maintains the absolute source of truth for the physical wiring between the Radxa Rock 3A and the Inkplate 4 TEMPERA.

## Current Setup: Direct SPI Wiring (No Sniffer)

*Orientation: Radxa 40-pin header on the right, USB ports facing down. Pin 1 and 2 at the top.*
*Inside Column = Left column (odd pins). Outside Column = Right column (even pins).*

| Radxa Location | Radxa Pin # | Wire Color | SPI Function | Connects To | Inkplate Label | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Inside Col, 10th down | **Pin 19** | **Purple** | MOSI (Data In) | ➔ | **IO 13** | ESP32 Data RX |
| Inside Col, 11th down | **Pin 21** | **Black** | MISO (Data Out)| ➔ | *(Disconnected)* | *Disabled in code (-1)* |
| Inside Col, 12th down | **Pin 23** | **White** | CLK (Clock) | ➔ | **IO 14** | ESP32 SPI Clock |
| Outside Col, 12th down| **Pin 24** | **Blue** | CS0 (Chip Select)| ➔ | **IO 15** | *Moved from SCL to fix display crash* |
| Outside Col, 3rd down | **Pin 6** | **Grey** | GND (Ground) | ➔ | **GND** | Required for common ground |

*Note: The Logic Analyzer / Sniffer has been removed from this circuit to prevent power draw issues during e-ink display updates.*
