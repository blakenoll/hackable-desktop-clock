#pragma once
// Behaviour knobs in one place: timings, thresholds and limits people commonly want to change.
// Visual layout (pixel offsets, ring geometry, colours) lives with the UI code instead; see docs/CUSTOMIZING.md.
// Header-only constants, safe to include anywhere (also compiled by the host renderer, docs/tools/sim).
#include <stdint.h>

// ---- Boot and network (src/main.cpp) ----------------------------------------------------------------
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20UL * 1000;  // no WiFi after this at boot -> setup mode
static constexpr const char *NTP_SERVER_1 = "pool.ntp.org";
static constexpr const char *NTP_SERVER_2 = "time.nist.gov";
static constexpr uint32_t OFFLINE_MARKER_MS = 10UL * 1000;      // WiFi down this long -> slashed WiFi glyph (blips don't show)
static constexpr const char *DEFAULT_TZ = "PST8PDT,M3.2.0,M11.1.0";  // before one is saved (src/config.cpp)

// ---- BOOT button (src/hw/button.cpp) ------------------------------------------------------------------
static constexpr uint32_t BUTTON_LONG_PRESS_MS = 3000;                     // hold -> setup mode
static constexpr uint32_t BUTTON_SHORT_MIN_MS = 40, BUTTON_SHORT_MAX_MS = 1000;  // debounce .. clearly not a hold
static constexpr uint32_t BUTTON_DOUBLE_MS = 350;  // second press must start within this of the first release

// ---- Setup mode (src/setup/) ----------------------------------------------------------------------------
static constexpr const char *SETUP_AP_PREFIX = "clk-setup";        // + "-A1B2" from the chip's MAC
static constexpr uint32_t SETUP_IDLE_REBOOT_MS = 5UL * 60 * 1000;  // no client this long -> reboot and retry saved WiFi
static constexpr uint32_t SETUP_SCAN_TIMEOUT_MS = 15UL * 1000;     // WiFi scan before the AP starts

// ---- Settings API (src/api/, docs/API.md) -----------------------------------------------------------------
static constexpr const char *API_HOSTNAME_PREFIX = "deskclock";  // + "-a1b2" from the chip's MAC -> deskclock-a1b2.local
static constexpr uint16_t API_PORT = 80;
static constexpr int PAIR_WINDOW_SECONDS = 60;  // how long POST /api/pair waits for a BOOT press before giving up

// ---- Weather (src/data/weather.cpp, weather_format.cpp) --------------------------------------------------
// Open-Meteo's "current" data itself updates about every 15 min, so much shorter mostly returns unchanged values.
static constexpr uint32_t WEATHER_REFRESH_MS = 10UL * 60 * 1000;
static constexpr uint32_t WEATHER_RETRY_MS = 60UL * 1000;        // after a failed fetch
static constexpr uint32_t WEATHER_STALE_MS = 30UL * 60 * 1000;   // last good fetch older than this -> row dimmed
static constexpr int RAIN_SHOW_PCT = 30;                         // detail line shows "40% rain" from this chance up
static constexpr int FEELS_DIFF_F = 5, FEELS_DIFF_C = 3;         // ...else "Feels 65°" when it differs this much
static constexpr float WIND_CALM_MPH = 3.0f, WIND_CALM_KMH = 5.0f;  // below: "CALM", compass needle hidden

// ---- Tides (src/data/tide.cpp) ----------------------------------------------------------------------------
static constexpr uint32_t TIDE_REFRESH_MS = 6UL * 60 * 60 * 1000;  // predictions don't change; keeps the window current
static constexpr uint32_t WATER_REFRESH_MS = 30UL * 60 * 1000;     // water temperature
static constexpr float TIDE_MAX_STATION_KM = 60;                   // nearest station farther than this: "no tides here"

// ---- Radar (src/data/aircraft.cpp) ------------------------------------------------------------------------
static constexpr int RADAR_NM = 5;                             // query radius and the scope's outer ring, nautical miles
static constexpr uint32_t RADAR_POLL_MS = 10UL * 1000;         // adsb.fi allows 1 request/s; the face dead-reckons between
static constexpr uint32_t RADAR_BACKOFF_MS = 60UL * 1000;      // after an HTTP error (4xx responses count toward an IP ban)
static constexpr uint32_t RADAR_STALE_MS = 2UL * 60 * 1000;    // fetches failing this long -> "NO RADAR DATA"

// ---- Notifications (src/ui/notify.cpp, src/api/api.cpp) -------------------------------------------------
static constexpr int NOTIFY_DEFAULT_SECONDS = 30;  // how long a card shows when the request gives no "duration"
static constexpr int NOTIFY_MAX_SECONDS = 3600;    // longest "duration"; 0 means until dismissed
