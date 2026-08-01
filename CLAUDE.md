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

## 4. SPI Transport Layer (V4 Firmware — still in use)

### **Framing: CS-less SCLK-Timeout**
CS (Pin 15) is unreliable on the Inkplate PCB (see §5), so `SCLK` does double duty as both clock and frame delimiter.

*   Master waits for Handshake HIGH, then sends 256-byte SPI transfer.
*   Slave counts SCLK rising edges via IRAM-pinned ISR into a 256-byte `rx_buf`.
*   If SCLK is silent for > 50 ms and exactly 2048 bits were received, `transfer_complete` is set.
*   After each display refresh, `reclaim_pin15_for_gpio()` re-asserts IO_MUX ownership of Pin 15 (the Inkplate library silently reclaims it during `display.display()`).

**Risk:** A single noise pulse on SCLK shifts the bitstream. Mitigated by exact-bit-count check; no CRC yet.

### **Key firmware constants**
```cpp
#define PAYLOAD_BYTES 256   // must match kyphone_os.py PAYLOAD_BYTES
#define TOTAL_BITS    2048  // PAYLOAD_BYTES * 8
#define PIN_MOSI 13
#define PIN_SCLK 14
#define PIN_CS   15
#define PIN_HANDSHAKE IO_PIN_B0
```

## 5. The Pin 15 Problem — Everything You Know
*   **Symptoms:** `SCLK` interrupts fire correctly. `CS` (Pin 15) interrupts fire 0 times, or fire hundreds of times (noise) but never a clean framing pulse.
*   **Hypothesis 1 (IO_MUX):** The `Inkplate.h` library calls `SPI.begin()`, which sets `IO_MUX_GPIO15_REG` to Function 1 (SPI CS0). This routes the pin directly to the SPI peripheral, bypassing the GPIO Matrix required for `attachInterrupt()`. **Status: Confirmed — `reclaim_pin15_for_gpio()` is the workaround, but the library re-steals it on every refresh.**
*   **Hypothesis 2 (Strapping Pin):** GPIO 15 is `MTDO`. It has a boot-time pull-down. On the Inkplate PCB, it might be heavily clamped to 0V or 3.3V to ensure boot stability. **Status: Mitigating with internal PULLUP.**
*   **Hypothesis 3 (Library Theft):** The Inkplate library re-initializes the display peripheral during `display.display()` or `display.einkOff()`, silently re-claiming Pin 15 for the SPI hardware.

### **What Has NOT Been Tried**
*   **Hardware Filtering:** Adding a small capacitor to Pin 15 to damp ringing.
*   **Level Shifting:** Confirming if the Radxa (3.3V) and Inkplate (3.3V) have a voltage delta causing the "Invisible 0" on Pin 15.
*   **ESP-IDF GPIO Driver Only:** Completely bypassing Arduino `attachInterrupt`/`pinMode` and using low-level `gpio_isr_handler_add` after manual IO_MUX config.

### **Open Questions**
*   Why does `SCLK` succeed at the IO_MUX level when Pin 15 fails, even though both are used by the E-ink controller? (Theory: SCLK is an output from the ESP32 to the display, leaving its input-sense matrix path open.)
*   Does `display.einkOff()` fully release the SPI bus or just cut power to the panel?

## 6. OS 0.1 — Application Layer

### **Version tagging**
*   `5a32d00` — tagged `os-0.0`: 3-screen app (`kyphone_app.py`), screens: HOME / MSG_LIST / MSG_THREAD.
*   `039042d` — OS 0.1: 6-screen state machine (`kyphone_os.py`), TDD test suite, updated firmware renderers.

### **State machine**
`kyphone_os.py` is the production entry point. Single `state['screen']` string drives all rendering. Supporting state:

```python
state = {
    'screen': 'lock',            # lock | home | texts_list | thread | compose | stub
    'home_index': 0,             # 0=TEXT 1=CALL(Phase2) 2=READ 3=LISTEN
    'texts_index': 0,            # -1=back header sel, -2=plus header sel
    'texts_header_sel': 'back',  # 'back' | 'plus'
    'thread_id': None,           # sender phone number
    'thread_draft': '',
    'compose_to': '', 'compose_msg': '', 'compose_to_active': True,
    'stub_name': '', 'quote_index': 0,
    'messages': [], 'last_sid': None, 'running': True,
    'lock': threading.Lock(),
}
```

`get_threads()` groups flat `state['messages']` by sender, sorted newest-first, returns `[{sender, name, messages, unread}]`.

### **Key transitions**
| From | Key | Effect |
| :--- | :--- | :--- |
| lock | any | → home (home_index=0) |
| home | ↑↓ | cycle home_index 0–3 |
| home | Enter | TEXT→texts_list; READ/LISTEN→stub; (CALL→Phase 2) |
| home | Esc | → lock (advance quote_index) |
| texts_list | ↑↓ | move texts_index; ↑ past 0 → header (index=-1) |
| texts_list | ←→ (header) | toggle texts_header_sel back/plus |
| texts_list | Enter (row) | → thread (mark read) |
| texts_list | Enter (back) / Esc | → home |
| texts_list | Enter (plus) / `CHAR:+` | → compose |
| thread | `CHAR:<c>` | append to thread_draft |
| thread | Backspace | delete last char from draft |
| thread | Enter (non-empty draft) | send via Twilio + append outgoing; clear draft |
| thread | Enter (empty draft) | no-op |
| thread | Esc | → texts_list |
| compose | `CHAR:<c>` | append to active field (to/msg) |
| compose | Tab | toggle compose_to_active |
| compose | Enter (TO non-empty) | move focus to MESSAGE |
| compose | Enter (both non-empty) | create thread → navigate to thread |
| compose | Esc | → texts_list |
| stub | Esc | → home |

### **SPI command protocol**
All commands: `PREFIX|arg1|arg2|…` — pipe-delimited, null-terminated, fits in 253 chars (PAYLOAD_BYTES=256 minus 3-byte header `[0x00, 0x00, 0x02]`).

| Screen | Command format |
| :--- | :--- |
| Lock | `LOCK\|{HH:MM AM/PM}\|{DAY, MON DD}\|{quote}\|{attribution}` |
| Home | `HOME2\|{HH:MM AM/PM}\|{home_index}\|{unread_count}` (index 0–3, or -1 = KYPHONE header selected) |
| Texts list | `TEXTS\|{selected_idx}\|{name·preview·unread}\|…` (idx -1=back, -2=plus) |
| Thread | `THREAD2\|{name}\|{draft}\|{hdr}\|{Y:body or R:body}\|…` (last 4 msgs; hdr: ''=typing, 'B'=back selected, 'I'=info selected) |
| Compose | `COMPOSE\|{to_field}\|{msg_field}\|{1=to_active, 0=msg_active}\|{hdr}` (hdr: ''=typing, 'X'=exit selected) |
| Stub | `STUB\|{app_name}` |

`HOME2`/`THREAD2` prefixes avoid collision with OS 0.0 firmware commands during rollout.

### **Character input convention**
Both input paths forward printable keypresses as `CHAR:<char>`:
*   **`input_handler.py` (hardware Radxa):** evdev key events; shift-key tracking for symbols/uppercase; dispatches `on_key(f'CHAR:{char}')`.
*   **`simulator.py` (Mac --sim):** pygame `event.unicode` for any single printable character not already in `KEY_MAP`; spawns daemon thread → `on_key(f'CHAR:{char}')`.

`handle_key()` in `kyphone_os.py` checks `keycode.startswith('CHAR:')` to route to the active draft field.

### **Inkplate firmware renderers (OS 0.1)**
Six new functions added to `Inkplate_SPI_Peripheral.ino` before `setup()`, dispatched from `loop()` before the legacy SMS fallback:

*   `render_lock(data)` — OS 0.1 label + ASCII cat (bottom, cat is a fixed 1-bit bitmap via `drawBitmap()`, not live text — see Known Constraints), textSize-8 clock (y=190), date (y=270), quote/attribution word-wrapped via `render_centered_wrapped` (starts y=390)
*   `render_home2(data)` — KYPHONE header (highlights when `home_index=-1`) + time, 4 rows sized to fill the full remaining screen height (no footer), selected row inverted with `fillRect`
*   `render_texts(data, selected)` — `<`/TEXT/`+` header, 88px thread rows with name·preview·unread badge
*   `render_thread2(data)` — `<`/NAME/`i` header (highlights on `thread_header_sel` = `back`/`info`), AIM-style message bubbles, reply bar at y=558 with `> ` + draft + cursor block
*   `render_compose(data)` — NEW MESSAGE header with `X` (highlights on `compose_header_sel`='x', exits same as Esc), TO:/MESSAGE: fields, `SEND` visual affordance (bottom right, same action as Enter), cursor block in active field
*   `render_stub(data)` — app name + COMING SOON

OS 0.0 renderers (`render_home`, `render_msg_list`, `render_msg_thread`) kept for rollback safety.

### **Tests**
`spi_bridge/tests/test_state_machine.py` — 38 pytest tests across 5 classes (TestLockScreen, TestHomeScreen, TestTextsListScreen, TestThreadScreen, TestComposeScreen). Hardware mocked at import time via `sys.modules.setdefault(...)`. All tests patch `kyphone_os.push_screen` to intercept SPI without hardware.

```
python3 -m pytest spi_bridge/tests/test_state_machine.py -v
```

### **Simulator**
```
python3 spi_bridge/kyphone_os.py --sim
```
Renders all 6 screens in a 600×600 pygame window. Full keyboard navigation including shift-modified characters. No hardware required.

### **Phase 2 (Calls) — out of scope for OS 0.1**
`home_index=1` (CALL) has a placeholder `# CALL — Phase 2` in `handle_key`. Calls flow (calls_list, dial, outgoing, incoming, in_call) is not implemented.

### **Security / privacy constraints**
*   Phone numbers live only on Radxa in gitignored `data/contacts.json` and `data/messages.json` — never committed to GitHub.
*   Twilio credentials live only in Radxa `~/.bashrc`.
*   `kyphone_app.py` contains `--demo` flag and `demo_messages.json` logic for YouTube filming — **must not be committed to the public repo**.

## 7. File & Directory Map
*   `spi_bridge/kyphone_os.py` — OS 0.1 main entry point + state machine (replaces `kyphone_app.py` in production).
*   `spi_bridge/kyphone_app.py` — OS 0.0 app; kept on Radxa for demo-mode use; **not committed**.
*   `spi_bridge/simulator.py` — pygame simulator; run with `--sim`; mirrors all Inkplate primitives.
*   `spi_bridge/input_handler.py` — evdev USB keyboard reader; forwards nav keys + `CHAR:<c>` for printable input.
*   `spi_bridge/Inkplate_SPI_Peripheral/Inkplate_SPI_Peripheral.ino` — Slave firmware (V4 transport + OS 0.1 renderers).
*   `spi_bridge/tests/test_state_machine.py` — 38 pytest unit tests for the state machine.
*   `spi_bridge/tests/Signal_Detector.ino` — Diagnostic tool for raw pin counting.
*   `spi_bridge/tests/wire_verifier.py` — Toggles all pins slowly to verify physical continuity.
*   `spi_bridge/string_test.py` — Legacy single-string SPI send test (OS 0.0).
*   `planning/SPI_Handoff_Context.md` — Summary of hardware architecture.

## 8. Design Principles

### **Architectural principles**
How the system is built — decisions that shape every layer.

1.  **Unidirectional data flow.** Radxa is the sole source of truth; Inkplate is a stateless sink. Nothing is stored on the ESP32 between frames. The display never sends data back (the Handshake Ready line is the only return signal, and it is flow control, not data).
2.  **Single state dict, single lock.** All mutable state lives in one `state` dict. A single `threading.Lock()` protects it. No per-screen state objects, no event queues, no scattered globals.
3.  **Handshake-gated writes.** The master never fires SPI without confirming the slave's Ready line is HIGH. Skipping this causes display corruption. Non-negotiable.
4.  **Human-readable wire protocol.** Commands are pipe-delimited ASCII strings, not binary structs. Chosen for debuggability — any transfer can be read in a serial monitor without a decoder.
5.  **No ACK / fire-and-forget.** After the handshake is observed, the command is sent and the master advances state. There is no confirmation that the Inkplate rendered correctly. UI state always advances even if the display glitched.
6.  **Firmware is a dumb renderer.** All logic — state transitions, thread grouping, message formatting, Twilio calls — lives in Python on the Radxa. The Inkplate receives a fully-formed pipe-delimited string and draws it. Never put business logic in the .ino.

### **Process principles**
How we work on this project.

1.  **New entry point over modifying legacy files.** When a major rework is needed, write a new file (`kyphone_os.py`) rather than patching the old one (`kyphone_app.py`). The old file stays on the device for rollback or demo use; it just isn't committed.
2.  **Simulator first.** Every screen must be fully navigable via `--sim` on a Mac before any hardware work. This catches logic bugs without a flash cycle and lets the UI evolve independently of the Inkplate.
3.  **TDD for the state machine.** State transitions are pure Python and can be tested without hardware. Write tests before the implementation; patch `push_screen` to intercept SPI calls in tests.
4.  **Rollback safety.** Keep old firmware renderers alongside new ones until hardware confirms the new path works. Never delete `render_home`, `render_msg_list`, `render_msg_thread` until OS 0.1 is stable on device.
5.  **Privacy by architecture.** Phone numbers and Twilio credentials never touch git. They live only on the Radxa (`data/contacts.json`, `data/messages.json`, `~/.bashrc`). The public repo contains zero PII.
6.  **Exact-count framing, no silent corruption.** The SCLK-timeout approach only accepts a transfer if exactly 2048 bits arrived. Partial or noisy frames are discarded. No CRC yet — if one is added, it goes here, not in the renderer.

### **Known constraints / gotchas**
Non-obvious facts that will bite future maintainers if undocumented.

*   **`reclaim_pin15_for_gpio()` must be called after every `display.display()`**, not just at startup. The Inkplate library re-steals Pin 15 on every refresh. The firmware already does this — do not remove it.
*   **`PAYLOAD_BYTES = 256` must stay in sync** between `kyphone_os.py` and the `.ino`. There is no compile-time check. If they drift, transfers corrupt silently.
*   **`texts_index` negative encoding** is deliberate: `-1` = back header selected, `-2` = plus header selected. One integer encodes both row position and header button state without a separate field. Do not flatten this to a separate bool.
*   **`HOME2`/`THREAD2` command names** were chosen to avoid firmware collision during the OS 0.0→0.1 rollout transition. Once hardware is confirmed stable on OS 0.1, they can be renamed to `HOME`/`THREAD` in a future firmware wipe.
*   **The lock screen's ASCII cat is a fixed 1-bit bitmap (`cat_bitmap[]`, `drawBitmap()`), not live `display.print()` text.** Live text rendering of the cat's dense punctuation was visually distorting on real hardware (root cause never fully identified — ruled out partial-refresh fidelity, ghosting, and string/escape-sequence corruption via direct testing). A pre-rendered bitmap sidesteps the whole class of bug by removing font rendering from the equation entirely. Generated via PIL (Courier Bold, `stroke_width=1` for boldness) from `spi_bridge/`-adjacent tooling — if the art needs to change again, regenerate the byte array, don't hand-edit it. Position is computed from the bitmap's actual `CAT_BITMAP_W`/`CAT_BITMAP_H` (`600 - W - 6`, `600 - H - 6`), not hardcoded, so it can't drift off-screen if the art's size changes.

### **Product principles**
The device exists to be a revolt against the attention economy, not just a "dumb phone."

1.  **Presence over connection.** The phone is for people who are *around*, not people who aren't. Features that pull attention toward the absent (feeds, notifications, algorithmic content) are explicitly out of scope.
2.  **Minimalist but expressive.** Not a sterile black/white brick. The UI should feel handcrafted — ASCII art, AIM-style bubbles, real typographic choices. Limitations are a canvas, not a constraint.
3.  **Physical intent.** Hardware features should communicate social state (e.g., a "open for a chat" button). The device should be readable in sunlight and usable with presence, not against it.
4.  **Artist collaborations.** Limited editions (e.g., Tombolo) make the device a statement piece. The industrial design should be as considered as the software.
5.  **Limitations spark creativity.** E-ink refresh rate, 256-byte payloads, a 600px screen — these are the medium, not obstacles. Work with them.

## 9. Product Inspiration
KyPhone is more than a technical exercise; it is a revolt against the attention economy.

*   **The Problem:** Smartphones are designed to keep us connected to people who *aren't* around, often at the expense of those who *are*. They are purveyors of "social time" that cannibalize real-world presence.
*   **The Vision:** A "Minimal Phone" (not just a "Dumb Phone"). Moving away from the sterile black/white brick design toward something expressive, intentional, and worth owning.
*   **Current Priority:** OS 0.1 is deployed and live on hardware — firmware flashed to the Inkplate, Radxa systemd service switched to `kyphone_os.py`, hardware bring-up complete. Next: iterate on real-hardware feedback (timing, framing reliability, Phase 2 calls scoping).
