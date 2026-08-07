# Wiring

## ST7735 1.44" TFT (SPI)

| ST7735 Pin | ESP32-S3 GPIO | Notes                          |
|------------|---------------|---------------------------------|
| VCC        | 3.3V          |                                  |
| GND        | GND           |                                  |
| SCLK / SCL | GPIO 12       | SPI clock                       |
| MOSI / SDA | GPIO 11       | SPI data (write-only, no MISO)  |
| CS         | GPIO 10       | Chip select                     |
| DC / RS    | GPIO 9        | Data/Command select             |
| RESET      | GPIO 8        | Active low                      |
| BL / LED   | GPIO 13       | Backlight (tie to 3.3V instead if you'd rather not control it in software) |

Pin numbers are defined in `main/ui/st7735.h` -- change the `PIN_LCD_*`
macros there if your wiring differs.

> **Heads up:** ST7735 breakout boards from different manufacturers
> sometimes differ in color order (RGB vs BGR) or mirror/rotate the
> image. If that happens, try a different `MADCTL` value in
> `panel_init_sequence()` in `main/ui/st7735.c` (common alternatives to
> the default `0xC8`: `0x08`, `0x68`, `0xA8`) -- this is a hardware
> quirk, not a firmware bug.

## Buttons

| Button | ESP32-S3 GPIO | Wiring                                |
|--------|---------------|-----------------------------------------|
| NAV    | GPIO 4        | Button between GPIO and GND (internal pull-up enabled in firmware) |
| ACTION | GPIO 5        | Same as above                           |

No external pull-up resistors needed -- `buttons_init()` enables the
ESP32's internal pull-ups.

- **NAV**: short press -> next menu item
- **ACTION**: short press -> select/confirm, long press (~600ms) -> back / stop attack

## Power

Both the display and buttons run comfortably off the ESP32-S3's 3.3V
rail. If you add a battery later, budget for the display's backlight
current draw (varies by module, usually 20-40mA) on top of the ESP32-S3's
Wi-Fi TX current (can spike to ~300-400mA briefly).
