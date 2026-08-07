# BitRot WROOM Radio Board

This is the v1.1 dual-board upgrade for BitRot. The ESP32 WROOM acts as
a dedicated Wi-Fi radio coprocessor, taking all wireless work off the
ESP32-S3 so both can run simultaneously on independent channels.

## Why a second board?

The ESP32-S3 can only be on one Wi-Fi channel at a time. This means in
v1.0, starting a scan kills an active attack, and the UI blocks while
the radio is busy. With the WROOM handling all radio work, the S3 stays
free for UI, display, buttons, and BadUSB at all times.

## Wiring

```
ESP32-S3 (Main Board)       ESP32 WROOM (Radio Board)
┌──────────────────┐        ┌──────────────────┐
│ GPIO 17  (TX) ───┼───────►│ GPIO 16  (RX)    │
│ GPIO 18  (RX) ◄──┼────────┤ GPIO 17  (TX)    │
│ GND           ───┼───────►│ GND              │
│ 3.3V          ───┼───────►│ 3.3V             │
└──────────────────┘        └──────────────────┘
```

**Important:**
- TX → RX, RX → TX (crossed, not straight through)
- GND must be shared between both boards
- 3.3V from S3 powers the WROOM (check S3's 3.3V pin can source enough
  current -- S3 + WROOM together during Wi-Fi TX can draw ~500mA peak.
  If either board resets unexpectedly, use a separate 3.3V regulator for
  the WROOM instead.)

## Building and flashing

This is a separate ESP-IDF project from the main S3 firmware.

```bash
cd esp32-wroom-radio
idf.py set-target esp32
idf.py build
idf.py -p COM_WROOM flash monitor
```

Flash the WROOM via its own USB/UART port (the COM port on the WROOM
devkit), independently of the S3.

## How it works

The S3 is always the commander. At boot it sends `PING\n` over UART1
(GPIO 17/18). If the WROOM replies `PONG\n`, dual-board mode activates
and a `[DUAL]` badge appears on the BitRot main menu. If no reply, BitRot
falls back silently to single-board mode -- no recompile needed.

During attacks, the S3 sends commands (`SCAN`, `DEAUTH:bssid,ch`,
`BEACON:RANDOM`, `STOP`) and the WROOM executes them, sending back scan
results and `STATUS:pkts,secs` heartbeats every second so the S3 display
stays live.

See `main/radio/protocol.h` in the S3 project for the full protocol spec.

## Roadmap

- v1.1: Wi-Fi coprocessor (this firmware) ✅
- v1.2: Add Classic Bluetooth scanning mode to the WROOM
  (the WROOM has BT built in -- the S3 doesn't)
