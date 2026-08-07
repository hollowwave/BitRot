# BitRot

A custom Wi-Fi pentesting tool built on an ESP32-S3, with a menu-driven UI
on a small TFT display and two-button navigation. Built as a focused,
single-feature project rather than a do-everything toolkit: **Wi-Fi
attacks only** (deauth, evil twin, beacon flood), with no SD card, no
Bluetooth, and no web UI competing for the radio.

Intentionally corrupt networks through targeted deauthentication, rogue access
points, and beacon flooding — hence the name.

> ⚠️ **For authorized security research and education only.** See
> [Responsible Use](#responsible-use) below and the notice in `LICENSE`.

## Features

- **Deauthentication attack** -- broadcast mode, forging 802.11 deauth
  frames to disconnect clients from a target AP
- **Evil twin** -- clones a target AP's SSID on a rogue softAP and serves
  a captive-portal login page over HTTP, with DNS hijacked so any device
  probing for connectivity lands on the portal
- **Beacon flood** -- broadcasts forged beacon frames with random or
  custom SSIDs to spam nearby devices' Wi-Fi network lists
- Menu-driven UI on a 128x128 ST7735 TFT, navigated with two buttons
- Live status during attacks: packet counters, elapsed time, connected
  clients, captured credential count

## Hardware

| Component | Spec |
|---|---|
| MCU | ESP32-S3 WROOM-1 N16R8 (16MB flash / 8MB PSRAM) |
| Display | ST7735 1.44" TFT, 128x128, SPI |
| Input | 2x tactile buttons (NAV, ACTION) |

See [`docs/WIRING.md`](docs/WIRING.md) for the full pinout.

## Architecture

The project is deliberately structured around a **radio abstraction
layer** (`main/radio/radio_interface.h`). The UI and attack-orchestration
code never call `esp_wifi_*` directly -- they only call functions
declared in that header. `radio_local.c` implements those functions on
top of the ESP32-S3's own Wi-Fi radio.

```
┌─────────────────────────────────────┐
│              UI Layer                 │  display, menu, buttons
├────────────────────────────────────────┤
│         Attack Controller             │  attacks/deauth.c, evil_twin.c, beacon_flood.c
├────────────────────────────────────────┤
│         radio_interface.h             │  <-- abstraction boundary
├────────────────────────────────────────┤
│            radio_local.c              │  direct esp_wifi_* calls (this build)
└────────────────────────────────────────┘
```

This exists because the ESP32-S3 has a real hardware limitation: **it can
only be on one Wi-Fi channel at a time**, so it can't (for example) inject
deauth frames and capture a WPA2 handshake simultaneously, or run evil
twin while also scanning. A natural v1.1 upgrade is a second ESP32
dedicated entirely to the radio, talking to this board over UART -- and
because everything above the abstraction layer only calls
`radio_interface.h` functions, that upgrade is a new `radio_uart.c` file,
not a rewrite.

### Task layout (FreeRTOS)

- `ui_task` -- Core 0. Renders the display, reads button state, never blocks on Wi-Fi.
- Attack tasks (`deauth_task`, `beacon_task`, evil twin's DNS/HTTP servers) -- Core 1.
- All cross-task state goes through a single mutex-guarded `app_state_t` (see `main/app_state.h`) -- no direct coupling between UI and radio code.

### Display / storage footprint

No SD card, no SPIFFS, no font/image assets on flash, and no external
display library dependency. `main/ui/st7735.c` is a from-scratch SPI
driver (init sequence, framebuffer push) and `main/ui/font6x8.h` is a
generated 6x8 bitmap font (see `tools/gen_font.py` -- rasterized from a
monospace TTF and supersample-downscaled for legibility at that size,
regenerate/tweak it any time). The 128x128 RGB565 framebuffer (32KB)
lives in PSRAM. Total flash footprint is well under 1% of the board's 16MB.

## Building

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) v5.1+.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

No managed/external components to fetch -- everything the UI needs
(display driver, font) is vendored directly in `main/`.

## Usage

BitRot boots with a splash screen, then drops into a menu-driven interface:

```
Boot -> BitRot Splash (1.2s) -> Main Menu
                                  ├─ Deauth Attack ──> Scan ──> Select AP ──> Confirm ──> Attacking
                                  ├─ Evil Twin ─────> Scan ──> Select AP ────────────> Attacking
                                  └─ Beacon Flood ──> Random / Custom List ─────────> Attacking
```

- **NAV** (short press): next menu item
- **ACTION** (short press): select / confirm
- **ACTION** (long press, ~600ms): back / stop the running attack

## Known limitations (v1)

- **Single radio, single channel.** See the Architecture section above.
- **Deauth is broadcast-mode only** in the UI. The radio layer supports a
  targeted (single-client) mode too, but that needs a client-discovery UI
  that isn't built yet (see Roadmap).
- **Evil twin can't clone WPA2 encryption** -- the rogue AP runs open.
  This is a hardware/protocol limitation, not a bug: cloning a WPA2
  handshake requires the real password, which is what the attack is
  trying to discover in the first place. It works because many devices
  will still show the network and let a user attempt to connect.

## Roadmap / stretch goals

- [ ] Channel-hopping deauth (cycle through channels to hit multiple APs)
- [ ] Client discovery + targeted deauth mode in the UI
- [ ] SSID whitelist (never attack a hardcoded list of networks)
- [ ] Persist last-used settings to NVS across reboots
- [ ] EAPOL/WPA2 handshake capture display
- [ ] Serial CLI as an alternative to physical buttons (handy for demos)
- [ ] **v1.1: dual-ESP32 build** -- second WROOM board dedicated to the
      radio, freeing the main board to monitor/capture while attacking
      (see Architecture)

## Responsible Use

This is a research and learning tool. Use it only on networks and
devices you own, or where you have explicit written authorization to
test. Unauthorized use against networks you don't control can be illegal
in most jurisdictions. See `LICENSE` for the full notice.

## License

MIT -- see [`LICENSE`](LICENSE).
