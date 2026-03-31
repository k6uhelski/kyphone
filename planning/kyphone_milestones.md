# KyPhone Project Milestones
*Updated: March 30, 2026*

## ✅ Milestone 1: Stable SPI Bridge
Reliable unidirectional SPI communication from Radxa → Inkplate. 128-byte payload, 5kHz clock, handshake flow control. 1.7s end-to-end latency. Proven 5/5 consecutive sends.

## ✅ Milestone 2: KyPhoneOS v0.1
Full SMS app running on hardware. Twilio polling, navigation state machine (HOME → MSG_LIST → MSG_THREAD), keyboard input via evdev, message persistence, partial clock updates, pygame simulator for local development.

## 🔄 Milestone 3: Messaging Complete
Full messaging flow — compose, reply, contacts. Address book (phone number → name mapping). Outbound SMS via Twilio (pending 10DLC approval). Clean, intentional UI throughout.

## Milestone 4: CHILL Apps (READ + LISTEN)
- **READ:** eBook reader — parse epub/txt on Radxa, paginate to 600x600, navigate pages.
- **LISTEN:** Music playback — local MP3s via Radxa, track info on screen, physical key controls.
- **CALL:** Placeholder → real voice via modem when hardware arrives.

## Milestone 5: App Runtime
Modular app architecture — each app as a Python module with a standard interface (`render`, `handle_key`, `on_focus`). Home screen as a launcher. Enables future apps (maps, notes, etc.) without rewriting the core.

## Milestone 6: Untethered Hardware
Battery, enclosure, physical keyboard (Q10 arriving May), cellular modem (Quectel EC25-AF). Device works away from a desk.

## Milestone 7: Daily Driver
Polished enough to use as a real phone. Power management, reliable boot, OTA updates. The Dieter Rams version.
