# Customizing the clock

Find your change in the table, edit the place it names, then build (`pio run`), check the layout on your computer (`sh docs/tools/sim/render.sh`, see [FACES.md](FACES.md#checking-a-face-without-flashing)), and flash (`pio run -t upload`). Layout is easy to get slightly wrong on a round screen, so always look at the real display too. If you moved or renamed something, run `python3 docs/tools/check.py`.

Many things need no code at all. On the **setup page** (hold BOOT 3 s) you can set 12/24-hour time, date order, face, colour theme, seconds as digits, the hourly chime, the weather detail line, night mode (after sunset / fixed hours / never) and its colour, ZIP and street address, units and tide station. A short BOOT press cycles faces, a double press cycles themes.

## Where to change what

| I want to... | Edit | Also |
|---|---|---|
| Change how often weather / tides / radar refresh, timeouts, "stale" or "offline" delays, button timings | `src/tuning.h` | |
| Change when the detail line shows rain vs "feels like", or what counts as calm wind | `RAIN_SHOW_PCT`, `FEELS_DIFF_*`, `WIND_CALM_*` in `src/tuning.h` | |
| Add or change a colour theme | `THEMES[]` in `src/ui/theme.cpp` (append; the index is saved) | `PALETTES` in `site/index.html`, rerun `render.sh` |
| Add or change a night colour | `NIGHTS[]` in `src/ui/theme.cpp` | `PALETTES` in `site/index.html`, rerun `render.sh` |
| Change which element gets which theme colour | `makePalette()` in `src/ui/core.cpp`; per face: its `palette` hook | |
| Move things on a face / change a face | `build()` in `src/ui/faces/face_<name>.cpp` | [FACES.md](FACES.md) |
| Add a face | new `src/ui/faces/face_<name>.cpp`, `enum Face` (`src/ui/ui.h`), `FACES[]` (`src/ui/faces.cpp`) | [FACES.md](FACES.md#adding-a-face) |
| Seconds ring look (tick sizes, tail, colours) | `tickGeo()` / `styleTick()` / `TAIL_LEN` in `src/ui/ring.cpp` | |
| Remove a face's ring | `f.ring = K_NONE` in its face file | |
| Date wording / order | `dateUpdate()` in `src/ui/time_view.cpp` (builds `DateParts`); a face can lay it out itself (`FaceDef::date`) | |
| Time format details (leading zero, AM/PM) | `fmtHour()`, `ui_update()`, `placeAmpm()` (`src/ui/time_view.cpp`, `transitions.cpp`) | |
| Minute-change fade, weather fades | `swapText()` in `src/ui/transitions.cpp` | |
| Hourly chime | `startChime()` in `src/ui/ring.cpp` | |
| Weather icon or icon colour for a condition | `weather_icon()` in `src/data/weather_format.cpp` | new glyphs: [fonts README](../src/ui/fonts/README.md) |
| Text of the detail line / wind readout | `weather_detail_text()`, `weather_feels_text()`, `weather_wind_text()` in `src/data/weather_format.cpp` | |
| Fetch another weather field | `fetchCurrent()` in `src/data/weather.cpp`, `WeatherData` in `weather.h`, `showWeather()` in `src/main.cpp` | [DATA.md](DATA.md#weather) |
| Weather row layout (icon, temperature, range bar) | `mkWeather()` and `RANGE_*` in `src/ui/weather_view.cpp` | |
| Add a timezone | `ZONES[]` in `src/setup/zones.cpp` | |
| Add a setup-page setting | `src/config_fields.h` (checklist at the top) | see below |
| Setup page wording / styling | `PAGE_HEAD` and the `*Fields()` helpers in `src/setup/form.cpp` | |
| Setup access-point name | `SETUP_AP_PREFIX` in `src/tuning.h` | |
| Settings API: endpoints, checks, which settings restart, mDNS name | `src/api/api.cpp`, `API_HOSTNAME_PREFIX` in `src/tuning.h` | [API.md](API.md) |
| Setup screen on the display | `ui_show_setup()` in `src/ui/setup_screen.cpp` | |
| Boot animations (WiFi, sync, sonar) | `src/ui/boot_screens.cpp` | |
| Fonts / a new font size | `src/ui/fonts/` | [fonts README](../src/ui/fonts/README.md) |
| Night mode behaviour | `ui_set_night()` / `nightExec()` in `src/ui/core.cpp`, `updateNight()` in `src/main.cpp` | [ARCHITECTURE.md](ARCHITECTURE.md#night-mode) |
| Pins, display, board | `platformio.ini`, `src/hw/` | [HARDWARE.md](HARDWARE.md) |
| Non-US locations | see "Weather outside the US" below | |

## Colours

A theme (`Theme` in `src/ui/ui_internal.h`, rows in `THEMES[]`) has these colours, all `0xRRGGBB`:

| Field | Colours |
|---|---|
| `digits` | the time (and, at 85 %, the temperature) |
| `accent` | the weekday and other accents |
| `tick` | the current-second tick (also the swatch on the web flasher) |
| `sub` | secondary text: date, AM/PM, seconds, detail line |
| `colon` | dim text: the Polished colon, small labels on Forecast / Tide / Radar |
| `base` / `bright` / `dim` | seconds-ring greys: elapsed, comet tail, upcoming |

`makePalette()` (`src/ui/core.cpp`) assigns them to elements. A face can override some in its `palette` hook and colour its own objects in `paint`. Weather icon colours are per condition in `weather_icon()`.

At night every element uses the night colour (`NIGHTS[]`) at some brightness, `nightC(percent)`, blended by `mixDN(day, night)`. If you add a day colour, give it a night value the same way. Keep night colours warm (red, amber), because blue and green light hurts night vision. The seconds ring fades to black at night on most faces.

## Layout

Coordinates in face code are pixel offsets from the centre of the 240x240 screen. At vertical offset `y` there are about `2 * sqrt(120² - y²)` px of usable width; the seconds ring takes the outer ~12 px. On Polished, the date is at y −62, the time at 0, the weather at +62 and the detail line at +90. Smaller numbers sit closer to the time.

## Adding a setting to the setup page

1. Add the field to `struct ClockConfig` (`src/config.h`).
2. Add one line to `config_fields()` in `src/config_fields.h`: NVS key (≤ 15 characters, also the form field name), field, default and range. Loading, saving and reading the submitted form all go through this list.
3. Add the input to one of the `*Fields()` helpers in `src/setup/form.cpp`, with `name=` set to the same key (`field()`, `option()` and `onOff()` build a `<select>`).
4. Use it. A display option goes into `UiOptions` (`src/ui/ui.h`) through `uiOptions()` in `src/main.cpp`; anything else reads `cfg.<field>`.
5. The settings API ([API.md](API.md)) picks it up from `config_fields()` with no extra work. If it's only read at boot, add its key to `RESTART_KEYS` in `src/api/api.cpp`; if it applies live in some other way than `ui_set_options()`, handle it in `settingsChanged()` (`src/main.cpp`). Give it labelled `choices` in `SchemaWriter` there if it's a list, and server-side checks in `textProblem()` if it's text.

Settings are in NVS under the namespace `clock` and survive reflashing.

## Weather outside the US

The ZIP lookup is restricted to US 5-digit ZIPs: `countryCode=US` in `geocode()` (`src/data/weather.cpp`), and the `pattern` on the `city` input in `weatherFields()` (`src/setup/form.cpp`). Remove both to allow other postal codes or city names. Open-Meteo's geocoder understands a bare name or postal code, not free text like "Paris France". The street-address option uses the US Census geocoder and only works in the US. Tides are NOAA (US) only.

## Night mode without a ZIP

Choose *Night mode: Fixed hours* on the setup page (`scheduledNight()` in `src/main.cpp`).
