# desktop-clock

Analog desktop clock for an ESP32-C3 Mini (Lolin C3 Mini) with a 240x240 round GC9A01 display. Time via NTP over WiFi.

## Wiring

| GC9A01 | C3 Mini GPIO |
|--------|--------------|
| VCC    | 3V3          |
| GND    | GND          |
| SCL    | 10           |
| SDA    | 4            |
| CS     | 5            |
| DC     | 1            |
| RST    | 0            |
| BLK    | 3 (optional) |

BLK is optional: many modules have no such pin (backlight always on). If yours does, uncomment `-DTFT_BL=3` and `-DTFT_BACKLIGHT_ON=HIGH` in `platformio.ini`.

Pins are set in `platformio.ini` build flags. Avoid GPIO 2/8/9 (strapping pins).

## Build

`pio run -t upload`. No WiFi settings are baked in at build time.

## WiFi + timezone setup

On first boot (or whenever the saved network can't be joined for 20 s) the clock starts its own access point, **DeskClock-Setup**. The screen shows a QR code (scan to join) and the one-time password. Joining pops up a setup page, or browse to `http://192.168.4.1`. Pick your network, enter the password, choose a timezone, and Save; the clock reboots and joins your WiFi. Settings live in flash (NVS) and survive reboots.

To change them later, **hold the BOOT button for 3 seconds** while the clock is running. (Holding BOOT *during reset* enters the ROM bootloader instead.) If setup mode is left idle for 5 minutes and WiFi was previously saved, the clock reboots and retries it.

## Code layout

- `src/main.cpp`: display driver glue (TFT_eSPI → LVGL flush), boot flow (saved WiFi → NTP, else setup mode), main loop.
- `src/provisioning.cpp`: NVS settings, setup-mode AP + captive portal + web form, BOOT long-press.
- `src/ui.cpp`: LVGL clock face (round `lv_scale` with line needles for the hands, date label) and the setup screen (QR code).
- `include/lv_conf.h`: LVGL 9 config (enabled widgets, fonts, memory).
