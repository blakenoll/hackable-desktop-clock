# Customizing the clock

Recipes for the changes people ask for most, with the exact place to edit. Build with `pio run`, flash with `pio run -t upload`, then check the real display. Layout is easy to get slightly wrong on a round screen, so always look at it.

## How it fits together

Boot flow (`src/main.cpp`):

1. Init the display and LVGL, build the UI (`ui_init`), restore the last day/night mode.
2. Load saved settings from flash (`prov_load`). If there's no WiFi saved → setup mode (`prov_run`, never returns).
3. Join WiFi (20 s timeout → setup mode), sync time over NTP with the saved timezone, start the weather task (`weather_begin`).
4. `loop()` once a second: update the clock face (`ui_update`) and, when the weather task publishes new data, redraw the weather row. A short BOOT press switches to the next face; holding BOOT for 3 s enters setup mode at any time.

Rules of thumb:

- **Only the main loop touches LVGL.** The weather task writes to a mutex-protected struct (`weather_get()`); the loop copies it into the UI. Don't call `lv_*` functions from the task.
- **The display is round.** Anything near a corner is clipped by the bezel. Text at vertical offset `y` from center has about `2 * sqrt(120² - y²)` pixels of usable width. The seconds ring uses the outer ~12 px.
- **Flash budget:** the firmware is ~1.39 MB of the 3 MB `huge_app` partition. Fonts are the big cost (the 56 pt font alone is ~120 KB).

## Time and date

All in `ui_update()` in `src/ui.cpp`.

**12/24-hour time, date order, face, theme, ring, seconds digits, chime, detail line, night options** — these are setup-page settings (NVS keys `h24`, `datefmt`, `face`, `accent` (theme), `ring`, `secs`, `chime`, `detail`, `nmode`, `nstart`, `nend`, `ncolor`), passed to `ui_set_options()` as a `UiOptions` at boot. To add a theme, append a row to `THEMES[]` in `src/ui.cpp`, add an `<option>` in `buildForm()` and bump `THEME_OPTIONS` in `src/provisioning.cpp`.

**Date format details** — the date is two labels side by side: the weekday in red (`dayLabel`, from `strftime("%a")`) and `Nov 6` in gray (`dateLabel`, built from `strftime("%b")` and `t.tm_mday`; the day is printed with `%d` through `snprintf` rather than `strftime` so single digits have no leading zero). `datefmt` 0/1 picks the order; for other wording, change those lines in `ui_update()`; to show the weekday in the same color as the rest, give both labels the same color. The date uses a 20 pt font; keep it under about 14 characters so it fits inside the ring.

**Show seconds as digits instead of (or as well as) the ring** — add a label and set it from `t.tm_sec` each second; remove the ring as described below.

## Colors

Colours come from the theme (`THEMES[]` in `src/ui.cpp`: digits, accent, current-second tick, secondary text, dim text and three ring greys) and are assigned to elements in `applyTheme()`. Night colours are one base colour (`NIGHTS[]`: red, amber, dim white) at different brightness: `nightC(percent)`. Every colour is a day/night pair blended by `mixDN()`. Colors are `0xRRGGBB`. The table shows the default theme (Red) with the red night colour.

| Element | Day | Night |
|---------|-----|-------|
| Time | `0xFFFFFF` | `0xD01818` |
| Weekday | `0xFF4D4D` | `0xD01818` |
| Month and day | `0xA0A0A0` | `0x901010` |
| Temperature | `0xD0D8E0` | `0xC01515` |
| High / Low | `0xFF8A65` / `0x6FB6FF` | `0xC01515` / `0x801010` |
| Weather icon | per condition, in `weather_icon()` (`src/weather.cpp`) | `0xC01515` |
| Skeleton loader shapes | `0x4A4F57` | `0x4A0E0E` |
| WiFi connecting icon (lit / dim) | `0xFFFFFF` / `0x303030` | `0xC01515` / `0x2A0707` |
| Ring (current / elapsed / upcoming) | `0xFF3030` / `0x8A95A3` / `0x2A3138` | `0xFF2020` / `0x8A1414` / `0x420C0C` |

Keep night mode to reds: blue and green light is what hurts night vision. If you change a day color, change its night counterpart too.

## Layout

Each face is a `case` in `buildFace()` in `src/ui.cpp`, built from shared pieces (`mkTime`, `mkDateRow`, `mkWeather`, `mkDetail`, `mkOffline`); the numbers are pixel offsets from the centre. For the default face (`FACE_POLISHED`):

- Date: `mkDateRow(-62, ...)`, weather: `mkWeather(0, 62, ...)`, detail line: `mkDetail(0, 90)`. Smaller numbers = closer to the time.
- The time is a row of labels (hours, colon, minutes; AM/PM hangs off the right) in the 56 pt font, centred; date and weather sit at y = ∓62.
- Loading states: `ui_set_connecting()` shows an animated WiFi glyph (dot + 3 arcs lit in sequence by an `lv_timer`, built in `createWifi()`) during WiFi connect; `ui_set_weather_loading()` shows a pulsing skeleton (`skeleton`, an `lv_anim` on opacity) in the weather row until the first fetch resolves. Both only animate while the main loop calls `lv_timer_handler()`, so keep that call in any blocking wait in `setup()`.
- Fonts: Manrope (SIL OFL), generated into `src/font_manrope_14.c` (500 weight), `_20.c` (500), `_56.c` (600, digits and `:` only), `_caps_12.c` (700, `0x20-0x5F,0xB0,0xB7`: small caps, no lowercase), `_34.c` (600, digits `:` `-` `°`; Forecast), `_74.c` (700, digits; Stacked) and `_light_64.c` (300, digits and `:`; Zen). `src/font_weather_42.c` is the weather icon font at 42 px for Forecast. Regenerate with `npx lv_font_conv --font manrope-latin-<weight>-normal.woff --size <px> --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --lv-font-name manrope_<px> -r <ranges> -o src/font_manrope_<px>.c` (static weights from the `@fontsource/manrope` npm package; download into its own empty folder). Each size costs flash.

## Faces

`enum Face` in `src/ui.h`; the setup page and a short BOOT press (`prov_button_event()` -> `ui_set_face()` in `loop()`) select one. Everything on the face lives under `faceRoot` and is deleted and rebuilt by `rebuild()` when the face or an option changes, then refilled from cached state (time, weather, detail, stale, offline). A face only creates what it uses, so most object pointers can be null: use the null-safe helpers (`tc`, `bgc`, `setText`, `setHidden`, `swapText`).

**Add a face:** add an enum value before `FACE_COUNT`, a `case` in `buildFace()`, a default ring in `ringKind()`'s `def[]` table, any face-specific colours in `applyTheme()` and date wording in `dateUpdate()`, then an `<option>` in `buildForm()`.

**See it without flashing:** `sh docs/tools/sim/render.sh` compiles `src/ui.cpp` for your computer (needs gcc and one prior `pio run`) and writes renders of every face, night mode and several states to `docs/faces/`. Edit `docs/tools/sim/sim.cpp` to stage other situations.

Ring kinds (`RingKind`): `K_TICKS`, `K_DOTTICKS` (default face), `K_BLOCKS` (retro), `K_MARKERS` (analog dial), `K_ARC`, `K_ORBIT` (dot), `K_DAYLIGHT`. Tick geometry is in `tickGeo()`, colours in `styleTick()`. The boot comet and hourly chime only run on the tick kinds.

## Seconds ring

Radii, lengths and widths per ring kind are in `tickGeo()` in `src/ui.cpp`.

- **Single moving dot instead of a filling trail:** in `styleTick()`, make the elapsed (`i < sec`) color the same as the upcoming color.
- **Remove the ring:** choose *Seconds ring: Off* on the setup page, or make `ringKind()` return `K_NONE`.

## Setup screen and behavior

In `src/provisioning.cpp`:

| What | Where |
|------|-------|
| Access-point name prefix (the board's MAC suffix is added in `prov_run()`) | `AP_SSID_PREFIX` |
| BOOT hold time to re-enter setup | `LONG_PRESS_MS` |
| Idle time before setup reboots to retry saved WiFi | `IDLE_REBOOT_MS` |
| Timezone dropdown | the `ZONES[]` array (label + POSIX TZ string); add other zones there as POSIX TZ strings ([POSIX TZ list](https://github.com/nayarsystems/posix_tz_db)) |
| Page styling / text | `PAGE_HEAD` and `buildForm()` |

The WiFi connect timeout (20 s) is in `setup()` in `src/main.cpp`.

### Adding a setting to the setup page

Four places, all in `src/provisioning.*`:

1. Add the field to `struct ClockConfig` (`provisioning.h`).
2. Read it in `prov_load()` and write it in `saveConfig()`.
3. Add an input in `buildForm()`.
4. Read `server.arg("yourfield")` in the `/save` handler.

Then use `cfg.yourfield` in `setup()`/`weather_begin()`. Settings live in NVS under namespace `clock`.

## Weather

In `src/weather.cpp`:

- **Refresh interval:** `REFRESH_MS` (10 min; Open-Meteo's current data itself updates about every 15 min, so going much shorter mostly returns unchanged values). Failed fetches retry after `RETRY_MS` (60 s).
- **Other fields:** add them to the `current=`/`daily=` list in `fetchCurrent()` and read them with `findNum()` (a tiny parser; no JSON library). Add the value to `WeatherData` (`weather.h`) and `publish()`. [Open-Meteo parameters](https://open-meteo.com/en/docs).
- **Non-US locations:** the lookup is restricted to US ZIP codes via `countryCode=US` in `geocode()`, and the setup field accepts 5 digits (`pattern` in `buildForm()`). Remove both to allow city names or other postal codes (Open-Meteo's geocoder does not understand free text like "Paris France"; a bare name or a postal code works).
- **Which icon for which weather:** `weather_icon()` maps WMO weather codes to glyphs and colors.
- **Night mode without a ZIP:** choose *Night mode: Fixed hours* on the setup page (`scheduledNight()` in `src/main.cpp`).

### Adding or changing weather icons

Icons come from `src/font_weather.c`, a subset of the Weather Icons font generated with LVGL's `lv_font_conv` (needs Node).

1. Download `weathericons-regular-webfont.ttf` and `weather-icons.css` from [erikflowers/weather-icons](https://github.com/erikflowers/weather-icons) into a **new, empty folder** outside the repo (treat downloads as untrusted; don't run tools from inside it).
2. Find a glyph's codepoint in the CSS, e.g. `.wi-day-sunny:before { content: "\f00d"; }`.
3. Regenerate, listing every codepoint you want:

   ```
   npx lv_font_conv --font /path/to/weathericons-regular-webfont.ttf --size 26 --bpp 4 \
     --no-compress --no-kerning --format lvgl --lv-include lvgl.h --lv-font-name weather_icons \
     -r 0xf00d,0xf02e,0xf002,0xf086,0xf013,0xf014,0xf01c,0xf019,0xf0b5,0xf01b,0xf01a,0xf01e \
     -o src/font_weather.c
   ```
4. Use the glyph in `weather_icon()` as `""`. Keep the SIL OFL attribution in the README.

The icon size is the `--size` value above (currently 26; 32 clipped at the top of the weather row). Re-run with a different value to resize. The Manrope 14/20 fonts do include U+00B0 (`°`), so temperatures could show it; the weather text currently omits it.
