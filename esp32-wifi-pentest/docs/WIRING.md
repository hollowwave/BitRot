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

Pin numbers are defined in `main/ui/st7735.h`, change the `PIN_LCD_*`
macros there if your wiring differs.

## Buttons

| Button | ESP32-S3 GPIO | Wiring                                |
|--------|---------------|-----------------------------------------|
| NAV    | GPIO 4        | Button between GPIO and GND (internal pull-up enabled in firmware) |
| ACTION | GPIO 5        | Same as above                           |


- **NAV**: short press -> next menu item
- **ACTION**: short press -> select/confirm, long press (~600ms) -> back / stop attack
