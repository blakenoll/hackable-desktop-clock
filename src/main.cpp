#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <time.h>
#include "provisioning.h"
#include "ui.h"
#include "weather.h"

static TFT_eSPI tft;
static constexpr int W = 240, H = 240;
static bool g_night = false;  // current display mode; persisted so power-up matches the last known state
static lv_color_t drawBuf[W * 40];  // partial render buffer (40 lines)

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px, w * h, true);  // true = swap bytes
  tft.endWrite();
  lv_display_flush_ready(disp);
}

void setup() {
  Serial.begin(115200);
#ifdef TFT_BL  // optional: some modules have no BLK pin (backlight always on)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  tft.init();
  tft.fillScreen(TFT_BLACK);

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t *disp = lv_display_create(W, H);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  ui_init();
  ClockConfig cfg;
  bool haveCfg = prov_load(cfg);
  ui_set_options(cfg.h24, cfg.dateFmt, cfg.accent);
#ifdef FORCE_NIGHT  // dev preview (-DFORCE_NIGHT=1): pin night mode on, ignore is_day, persist nothing
  g_night = true;
#else
  g_night = prov_load_night();
#endif
  ui_set_night(g_night);
  ui_boot_splash();
  ui_set_connecting(true);

  if (!haveCfg) prov_run(false);  // first boot: no WiFi saved yet

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    // Long-press BOOT, or fail to connect for 20 s -> setup mode.
    if (prov_button_long_press() || millis() - start > 20000) prov_run(true);
    lv_timer_handler();
    delay(50);
  }
  ui_set_connecting(false);
  ui_set_syncing(true);
  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");
  tm t;
  while (!getLocalTime(&t, 100)) {
    if (prov_button_long_press()) prov_run(true);
    lv_timer_handler();
  }
  ui_set_syncing(false);
  weather_begin(cfg);
}

static constexpr uint32_t STALE_MS = 30UL * 60 * 1000;   // weather older than this is dimmed
static constexpr uint32_t OFFLINE_MS = 10UL * 1000;       // brief WiFi blips don't show the marker

// Short line under the weather: rain chance if it matters, else "feels like" if it differs noticeably.
static void weatherDetail(const WeatherData &w, char *out, size_t n) {
  out[0] = 0;
  if (w.rainPct >= 30) snprintf(out, n, "%d%% rain", w.rainPct);
  else if (fabsf(w.feels - w.temp) >= (w.unit == 'C' ? 3 : 5))
    snprintf(out, n, "Feels %d\xC2\xB0", (int)lroundf(w.feels));
}

void loop() {
  if (prov_button_long_press()) prov_run(true);
  static int lastSec = -1;
  tm t;
  if (getLocalTime(&t, 0) && t.tm_sec != lastSec) {
    lastSec = t.tm_sec;
    ui_update(t);
#ifdef CYCLE_NIGHT  // dev preview (-DCYCLE_NIGHT=1): flip day/night every 75 s to watch the transition; persists nothing
    static uint32_t lastFlip = millis();
    if (millis() - lastFlip > 75000) {
      lastFlip = millis();
      g_night = !g_night;
      ui_set_night(g_night);
    }
#endif

    static uint32_t offSince = 0;
    if (WiFi.status() == WL_CONNECTED) offSince = 0;
    else if (!offSince) offSince = millis() | 1;
    ui_set_offline(offSince && millis() - offSince > OFFLINE_MS);

    static uint32_t shownWx = 0;
    WeatherData w = weather_get();
    ui_set_weather_stale(w.valid && millis() - w.fetchedAt > STALE_MS);
    if (w.version != shownWx) {
      shownWx = w.version;
      char buf[32];
      ui_set_weather_loading(w.loading);
      if (!w.valid) ui_set_weather_detail("");
      if (w.loading) {
        // skeleton only; wait for the first result
      } else if (w.valid) {
#if !defined(FORCE_NIGHT) && !defined(CYCLE_NIGHT)
        if (w.isDay == g_night) {  // is_day flipped
          g_night = !w.isDay;
          ui_set_night(g_night);
          prov_save_night(g_night);
        }
#endif
        uint32_t color;
        const char *icon = weather_icon(w.code, w.isDay, &color);
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(w.temp));  // "80°" (the 20 pt font includes U+00B0)
        ui_set_weather(icon, color, buf, w.hasRange, (int)lroundf(w.lo), (int)lroundf(w.hi), w.temp, weather_fall(w.code));
        weatherDetail(w, buf, sizeof(buf));
        ui_set_weather_detail(buf);
      } else {
        ui_set_weather(nullptr, 0, w.error, false, 0, 0, 0);
      }
    }
  }
  lv_timer_handler();
  delay(5);
}
