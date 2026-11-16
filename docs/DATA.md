# Network data

Three background sources, all keyless public APIs, each in its own file under `src/data/`. Timings and thresholds are in `src/tuning.h`. None of this code touches LVGL: each source publishes a snapshot that `loop()` in `src/main.cpp` copies into the UI (see [ARCHITECTURE.md](ARCHITECTURE.md#threads-and-data)).

Shared rules:

- **HTTPS with `setInsecure()`** (no certificate check). This is a deliberate choice for public, read-only data.
- **One HTTPS session at a time:** hold a `NetLock` (`src/data/net.h`) for the whole session, including any streamed body. Two TLS sessions don't fit in the heap.
- **Big responses are stream-parsed,** never held in RAM: a small `Stream` subclass is fed by `HTTPClient::writeToStream()` (`StationFinder` in `tide.cpp`, `AcParser` in `aircraft.cpp`).
- **No JSON library:** `weather.cpp` uses the tiny `findNum()` style helpers.
- Tasks run at priority 1, the same as `loop()`.

## Location

The weather task resolves coordinates once and caches them in NVS (`geocity`, `lat`, `lon`). Tides and radar use the same coordinates.

- **ZIP** (setup field, NVS `city`, US 5-digit): Open-Meteo's geocoder with `countryCode=US`. **Why a ZIP:** Open-Meteo can't parse free text like `Charleston SC`, and a ZIP is unambiguous. An empty ZIP turns weather (and so tides, radar and sunset night mode) off; a ZIP it can't find shows `ZIP not found`.
- **Street address** (optional, NVS `addr`): when set, the US Census geocoder (`geocoding.geo.census.gov`, ~700 B JSON; `coordinates.x` = lon, `y` = lat) is asked for `addr + ", " + ZIP` instead (`geocodeAddress()`). This puts weather, tides and radar on the house rather than the ZIP centroid, which can be miles off. If the address can't be matched, it falls back to the ZIP centroid (serial log only). A network error retries. **Never print the address.**
- The cache key is `config_geo_key()` (`ZIP` or `ZIP|address`); `geocity` and `tidegeo` store it, so changing either the ZIP or the address re-geocodes. Compare against that, never against `cfg.city`.

## Weather

File: `src/data/weather.cpp`.

- Open-Meteo forecast API: current conditions (temperature, WMO code, `is_day`, apparent temperature, wind speed and direction), hourly precipitation probability and temperature for the next 6 h, and today's high, low, sunrise and sunset. Units follow the setup page (°F + mph or °C + km/h).
- Refreshed every `WEATHER_REFRESH_MS` (10 min), retried after `WEATHER_RETRY_MS` (60 s). After a success, a later failure keeps showing the last reading, dimmed once it's older than `WEATHER_STALE_MS`.
- To add a field: add it to the `current=` / `hourly=` / `daily=` lists in `fetchCurrent()`, store it in `WeatherData` (`weather.h`), and read it in `showWeather()` (`main.cpp`).
- **How it is shown** (`src/data/weather_format.cpp`, pure functions): `weather_icon()` maps WMO codes to Weather Icons glyphs and colours (night icons via `is_day`). `weather_detail_text()` returns "40% rain" (max chance over 6 h, at least `RAIN_SHOW_PCT`) or else "Feels 65°". `weather_wind_text()` returns "NW 12 MPH" or "CALM". `weather_fall()` picks the rain or snow particles.

## Tides

File: `src/data/tide.cpp`.

- NOAA CO-OPS predictions (no key): `interval=hilo&datum=MLLW&time_zone=gmt&range=72` starting yesterday (UTC), so the heights match tide tables.
- Run from the weather task after each weather fetch (`tide_refresh()`). Predictions are refetched every `TIDE_REFRESH_MS` (6 h) or when the station or units change.
- **Station:** the setup page's `tidestn` if set, else the nearest to the coordinates. It is found once per location by streaming NOAA's ~2 MB station list through `StationFinder` and cached in NVS (`tidegeo`, `tideauto`, `tidename`). `tideauto` is empty when nothing is within `TIDE_MAX_STATION_KM`, which shows "NO TIDE STATION NEARBY". Station IDs aren't all numeric (e.g. `TWC0389`). A typed-in station's name comes from NOAA's mdapi station JSON.
- **Water temperature** (every `WATER_REFRESH_MS`): the station's `water_temperature` sensor if it reported in the last 3 h, else Open-Meteo Marine `sea_surface_temperature` at the coordinates (a model value). NAN hides it.
- Flow: `tide_get()` -> `updateTide()` (`main.cpp`) -> `ui_set_tide()` / `ui_set_tide_info()` -> the Tide face.

## Radar

File: `src/data/aircraft.cpp`.

- adsb.fi open data: `GET https://opendata.adsb.fi/api/v3/lat/<lat>/lon/<lon>/dist/<RADAR_NM>` (no key). Terms: personal, non-commercial use, with attribution to adsb.fi (credited in the README and the web flasher footer). The limit is 1 request/s, and 4xx responses count toward a temporary IP ban.
- Its own task, created on first use. It polls every `RADAR_POLL_MS` **only while the Radar face is showing** (`aircraft_set_active()` from `updateRadar()`); otherwise it blocks on a task notification with no wake-ups. After an HTTP error it backs off for `RADAR_BACKOFF_MS`. If the `NetLock` isn't free within 2 s (e.g. the tide task is streaming the station list) it skips that poll (`NET_BUSY`).
- It needs the geocoded coordinates, so it shows "LOCATING" until the weather task has them, and "SET A ZIP FOR RADAR" without a ZIP.
- The response (~500 B per aircraft, tens of KB near an airport) is stream-parsed by `AcParser`, which keeps the `AircraftData::MAX` (16) nearest. Each aircraft's `dst` and `dir` (computed by the server from the query point) become x/y NM and are brought forward by `seen_pos`. Aircraft on the ground (`alt_baro: "ground"`) are dropped, or taxiing jets would crowd the middle.
- Data older than `RADAR_STALE_MS` shows "NO RADAR DATA". The face side is in [FACES.md](FACES.md).
