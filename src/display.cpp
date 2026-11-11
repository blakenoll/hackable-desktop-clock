#include "display.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

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

void display_init() {
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
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
}
