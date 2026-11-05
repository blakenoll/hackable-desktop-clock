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
| BLK    | 3            |

Pins are set in `platformio.ini` build flags. Avoid GPIO 2/8/9 (strapping pins).

## Build

1. `cp include/secrets.example.h include/secrets.h` and set WiFi + timezone.
2. `pio run -t upload && pio device monitor`

## Code layout

- `src/main.cpp`: display driver glue (TFT_eSPI → LVGL flush), WiFi + NTP, main loop.
- `src/ui.cpp`: LVGL clock face (round `lv_scale` with line needles for the hands, date label).
- `include/lv_conf.h`: LVGL 9 config (enabled widgets, fonts, memory).
