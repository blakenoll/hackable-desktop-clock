#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <time.h>
#include "provisioning.h"
#include "ui.h"

static TFT_eSPI tft;
static constexpr int W = 240, H = 240;
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
  ui_set_status("Connecting...");

  ClockConfig cfg;
  if (!prov_load(cfg)) prov_run(false);  // first boot: no WiFi saved yet

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    // Long-press BOOT, or fail to connect for 20 s -> setup mode.
    if (prov_button_long_press() || millis() - start > 20000) prov_run(true);
    lv_timer_handler();
    delay(50);
  }
  ui_set_status("Syncing time...");
  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");
  tm t;
  while (!getLocalTime(&t, 100)) {
    if (prov_button_long_press()) prov_run(true);
    lv_timer_handler();
  }
  ui_set_status(nullptr);
}

void loop() {
  if (prov_button_long_press()) prov_run(true);
  static int lastSec = -1;
  tm t;
  if (getLocalTime(&t, 0) && t.tm_sec != lastSec) {
    lastSec = t.tm_sec;
    ui_update(t);
  }
  lv_timer_handler();
  delay(5);
}
