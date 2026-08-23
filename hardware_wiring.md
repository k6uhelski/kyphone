# KyPhone Hardware Wiring Guide

This document maintains the absolute source of truth for the physical wiring between the Radxa Rock 3A and the Inkplate 4 TEMPERA.

## Current Setup: 3-Wire + Expander Handshake (Final Agreement)

*Orientation: Radxa 40-pin header on the right, USB ports facing down. Pin 1 and 2 at the top.*
*Inside Column = Left column (odd pins). Outside Column = Right column (even pins).*

| Radxa Location | Radxa Pin # | Radxa Wire Color | Function | Inkplate Pigtail Color | Inkplate Label | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Inside Col, 7th down | **Pin 13** | **Yellow** | **Handshake** | **Blue** | **P1-0** | **Expander Pin (Left Header)** |
| Inside Col, 10th down | **Pin 19** | **Purple** | MOSI (Data Out)| **Orange** | **IO 13** | Shared (Native) |
| Inside Col, 12th down | **Pin 23** | **White** | CLK (Clock) | **White** | **IO 14** | Shared (Native) |
| Outside Col, 12th down| **Pin 24** | **Blue** | CS0 (Chip Select)| **Blue** | **IO 15** | Shared (Native) |
| Outside Col, 3rd down | **Pin 6** | **Grey** | GND (Ground) | **Grey** | **GND** | Required for common ground |

*Note: This setup uses Radxa Pin 13 (Line 21) to poll the Inkplate's P1-0 expander pin. This is the only physical configuration confirmed to carry a 3.3V signal across the boards.*

**⚠️ Blue/Blue collision risk:** Two different Inkplate-side pigtails are both **blue** — the one for IO15 (traced back to Radxa's Blue wire) and the one for P1-0 (traced back to Radxa's Yellow wire). They are NOT interchangeable and look identical at a glance. Do not sort by Inkplate-side pigtail color alone when reattaching this pair — always trace back to which Radxa wire (Blue vs. Yellow) each pigtail continues to before connecting.
