// Host render of the real ui.cpp: draws each face into a 240x240 buffer and writes PPM files.
#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include "ui/ui.h"
static uint32_t now_ms = 1000;
uint32_t millis() { return now_ms; }
void delay(uint32_t ms) { now_ms += ms; }
static uint16_t fb[240 * 240];
static lv_color_t drawBuf[240 * 40];
static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  uint16_t *p = (uint16_t *)px;
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * 240 + x] = *p++;
  lv_display_flush_ready(d);
}
static void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 10) { now_ms += 10; lv_timer_handler(); } }
static void shot(const std::string &name) {
  FILE *f = fopen((name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; i++) {
    uint16_t c = fb[i];
    uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
static tm mk(int h, int m, int s) { tm t = {}; t.tm_hour = h; t.tm_min = m; t.tm_sec = s; t.tm_mday = 10; t.tm_mon = 10; t.tm_year = 126; t.tm_wday = 2; t.tm_yday = 313; return t; }
static void weather(bool rainy) {
  static const int rain[6] = {20, 40, 70, 85, 55, 25};
  if (rainy) ui_set_weather("\xEF\x80\x99", 0x4DA3FF, "72\xC2\xB0", true, 61, 78, 72, 0);
  else ui_set_weather("\xEF\x80\x82", 0xFFD66B, "72\xC2\xB0", true, 61, 78, 72, 0);
  ui_set_weather_detail("40% rain");
  static const int dry[6] = {0, 0, 5, 10, 0, 0}, temps[6] = {68, 71, 73, 74, 72, 69};
  ui_set_wind(315, "NW 12 MPH", "");
  ui_set_weather_extra(rainy ? rain : dry, 6, 11, 6 * 60 + 42, 17 * 60 + 21, temps);
  // Highs and lows like Charleston's (NOAA style), local times on the sim day and the next.
  static const int hm[][2] = {{3, 5}, {9, 34}, {15, 58}, {21, 42}, {27, 51}, {34, 12}, {40, 30}, {46, 20}};
  static const float hv[] = {0.7f, 5.9f, 1.0f, 4.9f, 0.6f, 5.8f, 0.9f, 5.0f};
  time_t tt[8]; float th[8]; bool hi[8];
  for (int k = 0; k < 8; k++) {
    tm b = {}; b.tm_year = 126; b.tm_mon = 10; b.tm_mday = 10; b.tm_hour = hm[k][0]; b.tm_min = hm[k][1]; b.tm_isdst = -1;
    tt[k] = mktime(&b); th[k] = hv[k]; hi[k] = k % 2 == 1;
  }
  ui_set_tide_info("CHARLESTON", 68.7f);
  ui_set_tide(tt, th, hi, 8, 'F', nullptr);
}
int main(int argc, char **argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t *disp = lv_display_create(240, 240);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  ui_init();
  UiOptions o;
  ui_set_options(o);
  ui_set_night(false);
  ui_boot_splash();
  static const char *names[] = {"polished", "arc", "stacked", "analog", "zen", "forecast", "retro", "daylight", "tide"};
  lv_mem_monitor_t mon;
  // 1. every face by day, switched in place like the BOOT button does
  tm t = mk(10, 47, 37);
  ui_update(t);
  weather(false);
  run(1500);
  for (int f = 0; f < FACE_COUNT; f++) {
    ui_set_face(f);
    if (f == FACE_FORECAST) weather(true);
    t = mk(10, 47, 38); ui_update(t);
    run(1500);
    shot(out + "/day-" + names[f]);
    lv_mem_monitor(&mon);
    printf("%-9s used %6u max %6u frag %u%%\n", names[f], (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.max_used, mon.frag_pct);
  }
  // 2. options: seconds digits + 24 h + other themes / rings
  for (int f = 0; f < FACE_COUNT; f++) {
    UiOptions v; v.face = f; v.seconds = true; v.theme = 1 + f % 6; v.h24 = f % 2; v.dateFmt = f % 2;
    ui_set_options(v);
    t = mk(f % 2 ? 20 : 12, 8, 5); ui_update(t);
    run(1500);
    shot(out + "/opt-" + names[f]);
  }
  // 3. night (instant, like boot) in each night colour
  for (int f = 0; f < FACE_COUNT; f++) {
    UiOptions v; v.face = f; v.nightColor = f % 3;
    ui_set_options(v);
    ui_set_night(true);
    run(46000);
    t = mk(21, 30, 12); ui_update(t);
    run(1500);
    shot(out + "/night-" + names[f]);
    ui_set_night(false);
    run(46000);
  }
  // 4. states: loading skeleton, message, offline + stale, on a few faces
  for (int f : {0, 2, 5}) {
    UiOptions v; v.face = f; ui_set_options(v);
    t = mk(9, 5, 50); ui_update(t);
    ui_set_weather_loading(true); run(900);
    shot(out + "/load-" + names[f]);
    ui_set_weather_loading(false);
    ui_set_weather(nullptr, 0, "ZIP not found", false, 0, 0, 0); ui_set_weather_detail(""); ui_set_weather_extra(nullptr, 0, -1, -1, -1);
    ui_set_offline(true); run(1500);
    shot(out + "/msg-" + names[f]);
    ui_set_offline(false); weather(false); ui_set_weather_stale(true); run(1500);
    shot(out + "/stale-" + names[f]);
    ui_set_weather_stale(false); run(1000);
  }
  lv_mem_monitor(&mon);
  printf("final used %u max %u\n", (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.max_used);
  return 0;
}
