#include "../ui_internal.h"

namespace ui {

// Weather in the centre, small time on top, hourly rain (or temperature) bars below, and a compass ring
// whose arrow shows where the wind comes from.
static lv_obj_t *compRing, *compTick[12], *compCard[4], *windNeedle;
static lv_point_precise_t compPts[12][2], needlePts[5];  // must outlive the line objects
static int windDeg = -1;
static char windStr[16] = "";
static constexpr float COMP_R = 111.0f;
static lv_obj_t *bars[RAIN_BARS], *barLbl[RAIN_BARS], *barTemp[RAIN_BARS];
static constexpr int BAR_W = 12, BAR_PITCH = 18, BAR_MAX = 24, BAR_BASE = 191;  // BAR_BASE: y of the bars' bottom edge

// ---- Compass ring: wind direction needle ----

// A line from radius r1 to r2 at bearing `deg`; lv_line points are relative to the object's bounding-box corner.
static void placeSeg(lv_obj_t *l, lv_point_precise_t *pts, float r1, float r2, float deg) {
  float x1, y1, x2, y2;
  polar(r1, deg, x1, y1);
  polar(r2, deg, x2, y2);
  float minx = fminf(x1, x2), miny = fminf(y1, y2);
  pts[0] = {(lv_value_precise_t)lroundf(x1 - minx), (lv_value_precise_t)lroundf(y1 - miny)};
  pts[1] = {(lv_value_precise_t)lroundf(x2 - minx), (lv_value_precise_t)lroundf(y2 - miny)};
  lv_line_set_points(l, pts, 2);
  lv_obj_set_pos(l, (int32_t)lroundf(minx), (int32_t)lroundf(miny));
}

// Arrow on the rim at the bearing the wind comes FROM, pointing inward (the way the wind blows).
static void placeArrow(float deg) {
  float a = deg * (float)M_PI / 180.0f, dx = sinf(a), dy = -cosf(a);  // outward unit vector; perpendicular is (-dy, dx)
  auto pt = [&](float r, float lat, float &x, float &y) {
    x = RING_CENTER + r * dx - lat * dy;
    y = RING_CENTER + r * dy + lat * dx;
  };
  const float tail = COMP_R + 3, tip = COMP_R - 19, back = 8, wing = 5.5f;
  float px[5], py[5];
  pt(tip + back, -wing, px[0], py[0]);  // barb
  pt(tip, 0, px[1], py[1]);             // tip
  pt(tip + back, wing, px[2], py[2]);   // other barb
  pt(tip, 0, px[3], py[3]);
  pt(tail, 0, px[4], py[4]);            // tail
  float minx = px[0], miny = py[0];
  for (int i = 1; i < 5; i++) { minx = fminf(minx, px[i]); miny = fminf(miny, py[i]); }
  for (int i = 0; i < 5; i++) needlePts[i] = {(lv_value_precise_t)lroundf(px[i] - minx), (lv_value_precise_t)lroundf(py[i] - miny)};
  lv_line_set_points(windNeedle, needlePts, 5);
  lv_obj_set_pos(windNeedle, (int32_t)lroundf(minx), (int32_t)lroundf(miny));
}

static void windUpdate() {
  if (windNeedle) {
    setHidden(windNeedle, windDeg < 0);
    if (windDeg >= 0) placeArrow((float)windDeg);
  }
}

static void buildCompass() {
  memset(compTick, 0, sizeof(compTick));
  memset(compCard, 0, sizeof(compCard));
  compRing = box(ringBox);
  lv_obj_set_size(compRing, 222, 222);
  lv_obj_center(compRing);
  lv_obj_set_style_radius(compRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(compRing, 1, 0);
  lv_obj_set_style_border_opa(compRing, LV_OPA_COVER, 0);
  static const char *CARD[4] = {"N", "E", "S", "W"};
  for (int i = 0; i < 12; i++) {
    if (i % 3 == 0) {  // N/E/S/W: a letter instead of a tick
      lv_obj_t *l = compCard[i / 3] = mkLabel(ringBox, &manrope_caps_12);
      lv_label_set_text(l, CARD[i / 3]);
      float x, y;
      polar(100, i * 30.0f, x, y);
      lv_obj_align(l, LV_ALIGN_CENTER, (int)lroundf(x - RING_CENTER), (int)lroundf(y - RING_CENTER));
      continue;
    }
    compTick[i] = lv_line_create(ringBox);
    lv_obj_remove_style_all(compTick[i]);
    placeSeg(compTick[i], compPts[i], COMP_R - 6, COMP_R - 1, i * 30.0f);
    lv_obj_set_style_line_width(compTick[i], 2, 0);
    lv_obj_set_style_line_rounded(compTick[i], true, 0);
  }
  windNeedle = lv_line_create(ringBox);
  lv_obj_remove_style_all(windNeedle);
  lv_obj_set_style_line_width(windNeedle, 4, 0);
  lv_obj_set_style_line_rounded(windNeedle, true, 0);
  lv_obj_add_flag(windNeedle, LV_OBJ_FLAG_HIDDEN);
}

// ---- Hourly bars ----

// Dry spell (max chance < 30 %) with hourly temperatures known: the bars show temperature instead
// (scaled between the coolest and warmest of the six hours, cool blue to warm amber).
static bool barsTempMode() {
  int mx = -1, ok = 0;
  for (int k = 0; k < RAIN_BARS; k++) {
    if (rainPct[k] > mx) mx = rainPct[k];
    if (tempHr[k] != -32768) ok++;
  }
  return mx < 30 && ok == RAIN_BARS;
}

static void tempRange(int &lo, int &hi) {
  lo = 32767; hi = -32768;
  for (int k = 0; k < RAIN_BARS; k++) { if (tempHr[k] < lo) lo = tempHr[k]; if (tempHr[k] > hi) hi = tempHr[k]; }
}

static void barsPaint() {
  if (!bars[0]) return;
  bool tm = barsTempMode();
  int lo = 0, hi = 0;
  if (tm) tempRange(lo, hi);
  for (int k = 0; k < RAIN_BARS; k++) {
    int p = rainPct[k] < 0 ? 0 : rainPct[k] > 100 ? 100 : rainPct[k];
    uint32_t day = tm ? mixHex(0x5FA8FF, 0xFFB454, hi > lo ? (hi - tempHr[k]) * 100 / (hi - lo) : 50) : mixHex(0x6FB6FF, 0x24384F, p);
    bgc(bars[k], mixDN(day, nightC(tm ? 40 : 18 + p / 2)));
    tc(barLbl[k], mixDN(THEMES[opt.theme].colon, nightC(45)));
    tc(barTemp[k], mixDN(0xE6E6E6, nightC(70)));
  }
}

static void barsUpdate() {
  if (!bars[0]) return;
  bool tm = barsTempMode();
  int lo = 0, hi = 0;
  if (tm) tempRange(lo, hi);
  for (int k = 0; k < RAIN_BARS; k++) {
    bool show = rainStart >= 0;
    setHidden(bars[k], !show);
    setHidden(barLbl[k], !show);
    setHidden(barTemp[k], !show || !tm);
    if (!show) continue;
    int p = rainPct[k] < 0 ? 0 : rainPct[k] > 100 ? 100 : rainPct[k];
    int tmax = BAR_MAX - 10;  // leave room for the number above
    int h = tm ? 6 + (hi > lo ? (tempHr[k] - lo) * (tmax - 6) / (hi - lo) : (tmax - 6) / 2)
               : 3 + p * (BAR_MAX - 3) / 100;
    int x = 120 - (RAIN_BARS * BAR_PITCH - (BAR_PITCH - BAR_W)) / 2 + k * BAR_PITCH;
    lv_obj_set_size(bars[k], BAR_W, h);
    lv_obj_set_pos(bars[k], x, BAR_BASE - h);
    if (tm) {
      char tb[6];
      snprintf(tb, sizeof(tb), "%d", tempHr[k]);
      lv_label_set_text(barTemp[k], tb);
      lv_obj_update_layout(barTemp[k]);
      lv_obj_set_pos(barTemp[k], x + BAR_W / 2 - lv_obj_get_width(barTemp[k]) / 2, BAR_BASE - h - 17);
    }
    int hr = (rainStart + k) % 24;
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", opt.h24 ? hr : (hr % 12 == 0 ? 12 : hr % 12));
    lv_label_set_text(barLbl[k], buf);
    lv_obj_update_layout(barLbl[k]);
    lv_obj_set_pos(barLbl[k], x + BAR_W / 2 - lv_obj_get_width(barLbl[k]) / 2, BAR_BASE + 2);
  }
  barsPaint();
}

static void buildBars() {
  for (int k = 0; k < RAIN_BARS; k++) {
    bars[k] = box(content);
    lv_obj_set_style_radius(bars[k], 3, 0);
    lv_obj_set_style_bg_opa(bars[k], LV_OPA_COVER, 0);
    barLbl[k] = mkLabel(content, &manrope_caps_12);
    barTemp[k] = mkLabel(content, &manrope_caps_12);
  }
}

static void build() {
  buildCompass();
  mkDateRow(-85, 5, 1);
  mkTime(-59, &manrope_34);
  mkWeather(0, -6, false, 0, &manrope_34, &weather_icons_42);
  mkDetail(0, 30);
  buildBars();
  lv_obj_set_width(detailBox, 200);  // room for "NW 12 MPH · 40% RAIN"
  mkOffline(-66, -59);
}

static void palette(Palette &p, const Theme &) { p.day = p.sub; }

static void paint(const Theme &, const Palette &) { barsPaint(); }

static void paintRing(const Theme &T) {
  lv_obj_set_style_border_color(compRing, mixDN(mixHex(T.dim, 0x000000, 80), 0x000000), 0);
  for (int i = 0; i < 12; i++)
    if (compTick[i]) lv_obj_set_style_line_color(compTick[i], mixDN(T.base, 0x000000), 0);
  for (int i = 0; i < 4; i++) tc(compCard[i], mixDN(i == 0 ? T.tick : T.bright, 0x000000));
  lv_obj_set_style_line_color(windNeedle, mixDN(T.accent, 0x000000), 0);
}

static void onData() {
  barsUpdate();
  windUpdate();
}

static void onStale(lv_opa_t to, uint32_t ms) {
  for (int k = 0; k < RAIN_BARS; k++) { setOpa(bars[k], to, ms); setOpa(barTemp[k], to, ms); }
}

static const char *detailPrefix() { return windStr; }  // "NW 12 MPH"

static FaceDef make() {
  FaceDef f;
  f.name = "Forecast";
  f.ring = K_CUSTOM;
  f.build = build;
  f.date = dateQuietLine;
  f.palette = palette;
  f.paint = paint;
  f.paintRing = paintRing;
  f.onData = onData;
  f.onStale = onStale;
  f.detailPrefix = detailPrefix;
  f.msgInDetail = true;  // the big temperature has no room for "ZIP not found"
  return f;
}
extern const FaceDef faceDefForecast = make();

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

void ui_set_wind(int fromDeg, const char *text) {
  windDeg = fromDeg;
  snprintf(windStr, sizeof(windStr), "%s", text ? text : "");
  if (face().onData) face().onData();  // only the active face's hook runs, so no stale pointers are touched
  refreshDetail();
}
