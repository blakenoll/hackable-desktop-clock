# Architecture

How the firmware fits together. For "where do I change X", start with [CUSTOMIZING.md](CUSTOMIZING.md); for faces, [FACES.md](FACES.md); for the network data, [DATA.md](DATA.md).

## Source layout

```
src/
  main.cpp            boot flow and the once-a-second main loop
  tuning.h            timings and thresholds (refresh intervals, timeouts, button timings...)
  config.h/.cpp       ClockConfig and its NVS persistence
  config_fields.h     the list of user settings: NVS key, field, default, range
  hw/                 display.cpp (TFT_eSPI -> LVGL), button.cpp (BOOT button gestures)
  setup/              setup mode: portal.cpp (AP, DNS, web server), form.cpp (the page), zones.cpp (timezones)
  data/               background fetches: weather.cpp, tide.cpp, aircraft.cpp; weather_format.cpp (icons, text); net.h (NetLock)
  ui/                 everything drawn; public API ui.h, private ui_internal.h (lists the files)
    faces/            one file per clock face
    fonts/            generated LVGL fonts
include/lv_conf.h     LVGL configuration
fix_tft.py            pre-build patch for TFT_eSPI on the C3 (see HARDWARE.md)
docs/tools/           host renderer (sim/) and the docs consistency check (check.py)
site/                 the web flasher (GitHub Pages)
```

## Boot flow (`setup()` in `src/main.cpp`)

1. `display_init()`, `ui_init()`, then `config_load()`. The options are applied (`ui_set_options()`) **before** the boot splash so the first frame already has the right face and theme. The last day/night state is restored from NVS (`night`) so power-up matches.
2. `ui_boot_splash()`: a comet runs once around the ring (~1 s, blocking).
3. No WiFi saved -> `prov_run()` (setup mode, never returns).
4. `connectWifi()` with the animated WiFi glyph; after `WIFI_CONNECT_TIMEOUT_MS` or a long BOOT press -> setup mode.
5. `syncTime()` (NTP with the saved POSIX TZ) behind the "synchronizing" animation, then `weather_begin()` starts the weather task.

Every blocking wait calls `lv_timer_handler()` so the animations keep running.

## Main loop

`loop()` polls the button (short press = next face, double = next theme, both saved; long = setup mode), and once a second:

- `ui_update(t)`: time, date, ring, plus the face's `onTime` hook
- `updateNight()`: night by fixed hours when `nmode` is 1 (mode 0 follows `is_day` from the weather, mode 2 never)
- `updateOffline()`: the slashed WiFi marker after `OFFLINE_MARKER_MS` offline
- `updateWeather()` / `updateTide()` / `updateRadar()`: copy each background task's snapshot into the UI when its `version` changed. Weather older than `WEATHER_STALE_MS` is dimmed.

## Setup mode (`src/setup/`)

Entered on first boot, when WiFi can't be joined, or by holding BOOT for 3 s. `prov_run()` (`portal.cpp`) never returns:

1. It **scans WiFi before starting the access point**. **Why:** scanning while serving hops channels and drops the phone's connection. The scan is async (`WiFi.scanNetworks(true)` polled with `lv_timer_handler()`) so the sonar animation keeps running; a blocking scan would freeze it.
2. It starts a WPA2 AP named `SETUP_AP_PREFIX` plus the last two MAC bytes, with a **random 8-digit password per session**. The password is shown on screen (`ui_show_setup()`) and printed to serial.
3. A DNS server answers every name with the clock's IP, so phones open the captive portal. The page is built by `setup_page()` (`form.cpp`). On submit, `setup_parse()` reads it through `config_fields()`, then it saves and reboots.
4. If WiFi was saved before and no client connects for `SETUP_IDLE_REBOOT_MS`, it reboots to retry.

Only 2.4 GHz networks work (the C3 has no 5 GHz radio). Timezones are presets (`zones.cpp`); a saved zone that isn't a preset stays selectable as `Saved: ...`.

## Threads and data

- **Only the main loop (and setup mode, which replaces it) call LVGL.** Never call `lv_*` from a task.
- Background FreeRTOS tasks fetch data and publish it under a mutex: `weather_get()`, `tide_get()` and `aircraft_get()` return copies with a `version` counter that bumps on every change.
- The weather task also runs `tide_refresh()` after each weather fetch. The radar task runs on its own and only while the Radar face shows.
- All tasks run at priority 1, the same as `loop()`. **Why:** at priority 0 the radar's ~1 s TLS handshake took ~22 s behind the busy sweep and the server reset the connection.
- Only one HTTPS session runs at a time (`NetLock`, `src/data/net.h`). **Why:** two TLS sessions don't fit in the heap (`SSL - Memory allocation failed`).

## UI model

- `ui.h` is the public API; everything else in `src/ui/` is private (`ui_internal.h`).
- All face objects live under `faceRoot`: `ringBox` holds the ring and `content` the rest. `rebuild()` (`core.cpp`) deletes and recreates them whenever the face or an option changes, then refills them from cached state (`lastTm`, the weather, detail text, stale / offline / loading flags, rain hours, sunrise and sunset). That is why every setter caches what it shows.
- `content` stays hidden until the first `ui_update()` so the boot animations have the centre to themselves.
- A face creates only what it uses, so object pointers may be null. See [FACES.md](FACES.md) for the `FaceDef` hooks and rules.
- Colours: `makePalette()` (`core.cpp`) turns the active theme into text colours, a face may override some (`FaceDef::palette`), and `applyTheme()` paints everything.

## Night mode

- `ui_set_night()` sets the target. The first call (at boot) is instant; later flips cross-fade over ~45 s through an `lv_anim` that re-runs `applyTheme()` (throttled to steps of 4/255 of `nightMix`).
- Every colour is a day/night pair blended by `mixDN()` (`theme.cpp`). Night colours are one base colour (`NIGHTS[]`) at different brightness, `nightC(pct)`.
- The night face is deliberately quiet: the seconds ring (and the boot splash) fades to black and is hidden once fully faded, unless the face sets `ringAtNight` (Analog, Forecast). The range bar and high/low fade to black while the weather row slides sideways to stay centred (`rangeNightSync()`). There's no rain/snow animation or chime at night.
- The setup screen is unaffected. The board has no backlight control: a black dimming overlay was tried and removed, because backlight bleed made the blacks glow grey.
- Source of night: `nmode` 0 = `is_day` from the weather (needs a ZIP), 1 = fixed hours (`scheduledNight()`), 2 = never. The last state is persisted in NVS (`night`).
- Dev flags: `-DFORCE_NIGHT=1` pins night on; `-DCYCLE_NIGHT=1` flips every 75 s. Neither persists anything.

## Motion

- **Minute change:** only the labels that changed fade out, swap and fade back in (`swapText()`, 160 + 260 ms). The weather labels also rise 6 px; the time digits don't. **Why:** vertical movement made the whole time look like it bounced. The first draw is instant.
- **Weather updates** use the same `swapText()` fade; the icon colour is applied when its glyph swaps.
- **Loading states:** the animated WiFi glyph (`ui_set_connecting`) and the pulsing weather skeleton (`ui_set_weather_loading`, mirrors the real row's shapes) both have night colours.
- **Hourly chime:** a non-blocking comet lap around the ring at :00 (`startChime()` in `ring.cpp`, ~1.2 s, day only, tick rings only).
- **Rain / snow:** four particles drift down from the icon for ~4 s after an update (`startFall()`, day only).

## Flash and memory

- Partition `huge_app.csv`: ~3 MB app, no OTA. NVS stays at `0x9000`, so saved settings survive reflashing. `pio run` prints current flash and RAM use. The weather/TLS code alone is ~158 KB, which is why the default 1.3 MB app partition stopped being enough. Fonts are the next biggest cost.
- `LV_MEM_SIZE` is 64 KB (`include/lv_conf.h`). The host renderer prints LVGL heap use per face.
- `LV_INV_BUF_SIZE` is 64 (LVGL's default is 32). **Why:** with the Radar face's once-a-second blip moves the dirty-area list overflowed, and LVGL redrew the whole screen (a visible hitch).
- To measure redraw cost on the host, copy `docs/tools/sim/sim.cpp`, step `lv_timer_handler()` 33 ms at a time and sum the flushed pixels.
