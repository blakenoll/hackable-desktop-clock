# desktop-clock

Digital desktop clock for an ESP32-C3 Mini (Lolin C3 Mini) with a 240x240 round GC9A01 display. Time via NTP over WiFi.

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

On first boot (or whenever the saved network can't be joined for 20 s) the clock starts its own access point, **DeskClock-Setup**. The screen shows a QR code (scan to join) and the one-time password. Joining pops up a setup page, or browse to `http://192.168.4.1`. Pick your network, enter the password, choose a timezone, optionally a US ZIP code for weather and temperature units, and Save; the clock reboots and joins your WiFi. Settings live in flash (NVS) and survive reboots.

To change them later, **hold the BOOT button for 3 seconds** while the clock is running. (Holding BOOT *during reset* enters the ROM bootloader instead.) If setup mode is left idle for 5 minutes and WiFi was previously saved, the clock reboots and retries it.

## Display

- Seconds are shown as a ring of 60 ticks around the edge: elapsed seconds lit, the current second bright, upcoming dim.
- **Night mode:** when Open-Meteo reports it's dark (`is_day = 0`) everything switches to dim red tones for night-time viewing. It needs a ZIP code set, since the flag comes from the weather fetch; the last state is remembered across reboots.

## Code layout

- `src/main.cpp`: display driver glue (TFT_eSPI → LVGL flush), boot flow (saved WiFi → NTP, else setup mode), main loop.
- `src/weather.cpp`: Open-Meteo current conditions (ZIP geocoded once, refreshed every 15 min) shown above the time as an icon + temperature.
- `src/font_weather.c`: generated 32 px icon font (12 glyphs) from Weather Icons; see Credits.
- `src/provisioning.cpp`: NVS settings, setup-mode AP + captive portal + web form, BOOT long-press.
- `src/ui.cpp`: LVGL digital clock face (date on top, 12-hour time in the middle, weather icon + temperature + stacked high/low below, a 60-tick seconds ring around the rim), night mode, and the setup screen (QR code).
- `include/lv_conf.h`: LVGL 9 config (enabled widgets, fonts, memory).

## Credits

Weather icons: [Weather Icons](https://github.com/erikflowers/weather-icons) by Erik Flowers, font licensed under the SIL Open Font License 1.1. `src/font_weather.c` is a subset of it generated with `lv_font_conv`. Weather data: [Open-Meteo](https://open-meteo.com/).
