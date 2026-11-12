#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>
#include "button.h"
#include "config.h"
#include "display.h"
#include "provisioning.h"
#include "ui.h"
#include "tide.h"
#include "weather.h"
#include "weather_format.h"

static ClockConfig g_cfg;
static bool g_night = false;  // current display mode; persisted so power-up matches the last known state

// ---- Boot ------------------------------------------------------------------------------------------

static UiOptions uiOptions(const ClockConfig &cfg) {
  UiOptions o;
  o.h24 = cfg.h24;
  o.dateFmt = cfg.dateFmt;
  o.theme = cfg.accent;
  o.face = cfg.face;
  o.nightColor = cfg.nightColor;
  o.chime = cfg.chime;
  o.detail = cfg.detail;
  o.seconds = cfg.seconds;
  return o;
}

// Joins the saved WiFi. A long BOOT press, or failing for 20 s, drops into setup mode.
static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_cfg.ssid.c_str(), g_cfg.pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (button_long_press() || millis() - start > 20000) prov_run(true);
    lv_timer_handler();
    delay(50);
  }
}

static void syncTime() {
  configTzTime(g_cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");
  tm t;
  while (!getLocalTime(&t, 100)) {
    if (button_long_press()) prov_run(true);
    lv_timer_handler();
  }
}

void setup() {
  Serial.begin(115200);
  display_init();
  ui_init();

  bool haveCfg = config_load(g_cfg);
  ui_set_options(uiOptions(g_cfg));  // before the splash, so options apply from the first frame
#ifdef FORCE_NIGHT  // dev preview (-DFORCE_NIGHT=1): pin night mode on, ignore is_day, persist nothing
  g_night = true;
#else
  g_night = g_cfg.nightMode != 2 && config_load_night();  // mode 2 = never
#endif
  ui_set_night(g_night);
  ui_boot_splash();
  ui_set_connecting(true);

  if (!haveCfg) prov_run(false);  // first boot: no WiFi saved yet

  connectWifi();
  ui_set_connecting(false);
  ui_set_syncing(true);
  syncTime();
  ui_set_syncing(false);
  weather_begin(g_cfg);
}

// ---- Running ---------------------------------------------------------------------------------------

static constexpr uint32_t STALE_MS = 30UL * 60 * 1000;   // weather older than this is dimmed
static constexpr uint32_t OFFLINE_MS = 10UL * 1000;       // brief WiFi blips don't show the marker

// Night by the clock (setup page "Fixed hours"): from nightStart:00 until nightEnd:00, possibly across midnight.
static bool scheduledNight(int hour) {
  int a = g_cfg.nightStart, b = g_cfg.nightEnd;
  return a == b ? false : a < b ? (hour >= a && hour < b) : (hour >= a || hour < b);
}

static void setNight(bool n) {
  if (n == g_night) return;
  g_night = n;
  ui_set_night(n);
  config_save_night(n);
}

// BOOT: short press = next face, double press = next colour theme (both saved), long press = setup.
static void handleButton() {
  switch (button_event()) {
    case BTN_LONG: prov_run(true);
    case BTN_SHORT:
      g_cfg.face = (g_cfg.face + 1) % ui_face_count();
      ui_set_face(g_cfg.face);
      config_save_face(g_cfg.face);
      break;
    case BTN_DOUBLE:
      g_cfg.accent = (g_cfg.accent + 1) % ui_theme_count();
      ui_set_theme(g_cfg.accent);
      config_save_theme(g_cfg.accent);
      break;
    default: break;
  }
}

static void updateNight(const tm &t) {
#ifdef CYCLE_NIGHT  // dev preview (-DCYCLE_NIGHT=1): flip day/night every 75 s to watch the transition; persists nothing
  static uint32_t lastFlip = millis();
  if (millis() - lastFlip > 75000) {
    lastFlip = millis();
    g_night = !g_night;
    ui_set_night(g_night);
  }
#elif !defined(FORCE_NIGHT)
  if (g_cfg.nightMode == 1) setNight(scheduledNight(t.tm_hour));
#endif
}

static void updateOffline() {
  static uint32_t offSince = 0;
  if (WiFi.status() == WL_CONNECTED) offSince = 0;
  else if (!offSince) offSince = millis() | 1;
  ui_set_offline(offSince && millis() - offSince > OFFLINE_MS);
}

// Pushes new weather to the UI (only when the weather task published something new).
static void showWeather(const WeatherData &w) {
  char buf[32];
  ui_set_weather_loading(w.loading);
  if (!w.valid) ui_set_weather_detail("");
  if (w.loading) return;  // skeleton only; wait for the first result
  if (!w.valid) {
    ui_set_weather(nullptr, 0, w.error, false, 0, 0, 0);
    ui_set_weather_extra(nullptr, 0, -1, -1, -1);
    ui_set_wind(-1, "", "");
    return;
  }
#if !defined(FORCE_NIGHT) && !defined(CYCLE_NIGHT)
  if (g_cfg.nightMode == 0) setNight(!w.isDay);  // follows is_day from the weather
#endif
  uint32_t color;
  const char *icon = weather_icon(w.code, w.isDay, &color);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(w.temp));  // "80°" (the 20 pt font includes U+00B0)
  ui_set_weather(icon, color, buf, w.hasRange, (int)lroundf(w.lo), (int)lroundf(w.hi), w.temp, weather_fall(w.code));
  weather_detail_text(w, buf, sizeof(buf));
  ui_set_weather_detail(buf);
  char feels[16];
  weather_feels_text(w, feels, sizeof(feels));
  weather_wind_text(w, buf, sizeof(buf));
  ui_set_wind(weather_wind_calm(w) ? -1 : w.windDir, buf, feels);
  ui_set_weather_extra(w.rainHours, WeatherData::RAIN_HOURS, w.rainStartHour, w.sunriseMin, w.sunsetMin, w.tempHours);
}

static void updateWeather() {
  static uint32_t shownVersion = 0;
  WeatherData w = weather_get();
  ui_set_weather_stale(w.valid && millis() - w.fetchedAt > STALE_MS);
  if (w.version != shownVersion) {
    shownVersion = w.version;
    showWeather(w);
  }
}

// Pushes new tide data to the UI (the Tide face draws it).
static void updateTide() {
  static uint32_t shownVersion = UINT32_MAX;
  TideData d = tide_get();
  if (d.version == shownVersion) return;
  shownVersion = d.version;
  ui_set_tide_info(d.station, d.water);
  if (d.valid) ui_set_tide(d.t, d.h, d.high, d.n, d.unit, nullptr);
  else ui_set_tide(nullptr, nullptr, nullptr, 0, d.unit, !g_cfg.city.length() ? "SET A ZIP FOR TIDES" : d.error[0] ? d.error : "LOADING TIDES");
}

void loop() {
  handleButton();
  static int lastSec = -1;
  tm t;
  if (getLocalTime(&t, 0) && t.tm_sec != lastSec) {  // once a second
    lastSec = t.tm_sec;
    ui_update(t);
    updateNight(t);
    updateOffline();
    updateWeather();
    updateTide();
  }
  lv_timer_handler();
  delay(5);
}
