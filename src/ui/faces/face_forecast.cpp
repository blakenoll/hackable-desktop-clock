#include "../ui_internal.h"

namespace ui {

// Weather in the centre, small time on top, hourly rain (or temperature) bars below, and a compass ring
// with a wind line from the side the wind comes from to an arrowhead where it blows (hidden when calm).
static lv_obj_t *compRing, *compTick[12], *compCard[4], *windNeedle, *windTail, *windDot;
static lv_point_precise_t compPts[12][2], needlePts[5], tailPts[2];  // must outlive the line objects
static int windDeg = -1;
static char windStr[16] = "", feelsStr[16] = "";
static constexpr float COMP_R = 111.0f;
static constexpr int WIND_NIGHT = 35;           // nightC() brightness: the arrow stays, dim, on the quiet night face
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

// Wind line through the compass, like Apple Weather: a dot at the bearing the wind comes FROM, an arrowhead
// at the opposite side pointing the way it blows. The middle is left open for the weather readout.
static void placeArrow(float deg) {
  const float inner = 86, tailIn = 94, dotR = 107, tip = 107, back = 8, wing = 5.5f;  // tailIn: the dot's tail is shorter
  float to = deg + 180.0f;
  float a = to * (float)M_PI / 180.0f, dx = sinf(a), dy = -cosf(a);
  auto pt = [&](float r, float lat, float &x, float &y) {
    x = RING_CENTER + r * dx - lat * dy;
    y = RING_CENTER + r * dy + lat * dx;
  };
  float px[5], py[5];
  pt(inner, 0, px[0], py[0]);          // shaft start
  pt(tip, 0, px[1], py[1]);            // tip
  pt(tip - back, -wing, px[2], py[2]); // barb
  pt(tip, 0, px[3], py[3]);
  pt(tip - back, wing, px[4], py[4]);  // other barb
  float minx = px[0], miny = py[0];
  for (int i = 1; i < 5; i++) { minx = fminf(minx, px[i]); miny = fminf(miny, py[i]); }
  for (int i = 0; i < 5; i++) needlePts[i] = {(lv_value_precise_t)lroundf(px[i] - minx), (lv_value_precise_t)lroundf(py[i] - miny)};
  lv_line_set_points(windNeedle, needlePts, 5);
  lv_obj_set_pos(windNeedle, (int32_t)lroundf(minx), (int32_t)lroundf(miny));
  placeSeg(windTail, tailPts, tailIn, dotR, deg);
  float x, y;
  polar(dotR, deg, x, y);
  lv_obj_set_pos(windDot, (int32_t)lroundf(x) - 4, (int32_t)lroundf(y) - 4);
}

static void windUpdate() {
  if (windNeedle) {
    bool h = windDeg < 0;
    setHidden(windNeedle, h);
    setHidden(windTail, h);
    setHidden(windDot, h);
    if (!h) placeArrow((float)windDeg);
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
      polar(103, i * 30.0f, x, y);  // out by the rim, clear of the date row
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
  lv_obj_set_style_line_width(windNeedle, 3, 0);
  lv_obj_set_style_line_rounded(windNeedle, true, 0);
  lv_obj_add_flag(windNeedle, LV_OBJ_FLAG_HIDDEN);
  windTail = lv_line_create(ringBox);
  lv_obj_remove_style_all(windTail);
  lv_obj_set_style_line_width(windTail, 3, 0);
  lv_obj_set_style_line_rounded(windTail, true, 0);
  lv_obj_add_flag(windTail, LV_OBJ_FLAG_HIDDEN);
  windDot = box(ringBox);
  lv_obj_set_size(windDot, 8, 8);
  lv_obj_set_style_radius(windDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(windDot, LV_OPA_COVER, 0);
  lv_obj_add_flag(windDot, LV_OBJ_FLAG_HIDDEN);
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

// Rain mode labels only the wettest hour ("60%"); neighbouring numbers would collide.
static int rainPeak() {
  int best = 0;
  for (int k = 1; k < RAIN_BARS; k++)
    if (rainPct[k] > rainPct[best]) best = k;
  return best;
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
  int peak = rainPeak();
  for (int k = 0; k < RAIN_BARS; k++) {
    bool show = rainStart >= 0;
    bool num = tm || (k == peak && rainPct[peak] >= 10);
    setHidden(bars[k], !show);
    setHidden(barLbl[k], !show);
    setHidden(barTemp[k], !show || !num);
    if (!show) continue;
    int p = rainPct[k] < 0 ? 0 : rainPct[k] > 100 ? 100 : rainPct[k];
    int tmax = BAR_MAX - 10;  // leave room for the number above
    int h = tm ? 6 + (hi > lo ? (tempHr[k] - lo) * (tmax - 6) / (hi - lo) : (tmax - 6) / 2)
               : 3 + p * (tmax - 3) / 100;
    int x = 120 - (RAIN_BARS * BAR_PITCH - (BAR_PITCH - BAR_W)) / 2 + k * BAR_PITCH;
    lv_obj_set_size(bars[k], BAR_W, h);
    lv_obj_set_pos(bars[k], x, BAR_BASE - h);
    if (num) {
      char tb[8];
      if (tm) snprintf(tb, sizeof(tb), "%d", tempHr[k]);
      else snprintf(tb, sizeof(tb), "%d%%", p);
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
  mkDateRow(-83, 5, 1);
  mkTime(-59, &manrope_34);
  mkWeather(0, -6, false, 2, &manrope_34, &weather_icons);  // high/low stacked to the right
  mkDetail(0, 30);
  buildBars();
  lv_obj_set_width(detailBox, 200);  // room for "NW 12 MPH · FEELS 65°"
  mkOffline(-66, -59);
}

static void palette(Palette &p, const Theme &) { p.day = p.sub; }

static void paint(const Theme &, const Palette &) { barsPaint(); }

static void paintRing(const Theme &T) {
  lv_obj_set_style_border_color(compRing, mixDN(mixHex(T.dim, 0x000000, 80), 0x000000), 0);
  for (int i = 0; i < 12; i++)
    if (compTick[i]) lv_obj_set_style_line_color(compTick[i], mixDN(T.base, 0x000000), 0);
  // N is white rather than the accent, so it can't be mistaken for the (accent) wind arrow.
  for (int i = 0; i < 4; i++) tc(compCard[i], mixDN(i == 0 ? T.digits : T.bright, 0x000000));
  lv_color_t w = mixDN(T.accent, nightC(WIND_NIGHT));
  lv_obj_set_style_line_color(windNeedle, w, 0);
  lv_obj_set_style_line_color(windTail, w, 0);
  lv_obj_set_style_bg_color(windDot, w, 0);
}

// ---- Blinking colon: this face has no seconds ring, so the colon marks the seconds instead ----
// Every second the colon pulses: fades out (0-300 ms), stays out, fades back in from 500 ms (done by
// 800 ms) and stays lit. Quiet at night: solid.
static void colonExec(void *o, int32_t v) { lv_obj_set_style_text_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

static void onTime(const tm &, bool) {
  if (!timeC) return;
  lv_anim_delete(timeC, colonExec);
  if (nightMix != 0) {
    lv_obj_set_style_text_opa(timeC, LV_OPA_COVER, 0);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, timeC);
  lv_anim_set_exec_cb(&a, colonExec);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_duration(&a, 300);
  lv_anim_set_reverse_delay(&a, 200);
  lv_anim_set_reverse_duration(&a, 300);
  lv_anim_start(&a);
}

static void onData() {
  barsUpdate();
  windUpdate();
}

static void onStale(lv_opa_t to, uint32_t ms) {
  for (int k = 0; k < RAIN_BARS; k++) { setOpa(bars[k], to, ms); setOpa(barTemp[k], to, ms); }
}

// The whole detail line: wind, then "feels like" when it differs noticeably (the bars already show the
// rain chance). "NW 12 MPH · FEELS 65°"
static const char *detailPrefix() {
  static char buf[40];
  strlcpy(buf, windStr, sizeof(buf));
  if (feelsStr[0]) {
    if (buf[0]) strlcat(buf, " \xC2\xB7 ", sizeof(buf));
    strlcat(buf, feelsStr, sizeof(buf));
  }
  return buf;
}

static FaceDef make() {
  FaceDef f;
  f.name = "Forecast";
  f.ring = K_CUSTOM;
  f.build = build;
  f.date = dateQuietLine;
  f.palette = palette;
  f.paint = paint;
  f.paintRing = paintRing;
  f.onTime = onTime;
  f.onData = onData;
  f.onStale = onStale;
  f.detailPrefix = detailPrefix;
  f.detailOwn = true;
  f.ringAtNight = true;  // the wind arrow stays, dim; the rest of the compass fades to black
  f.msgInDetail = true;  // the big temperature has no room for "ZIP not found"
  return f;
}
extern const FaceDef faceDefForecast = make();

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

void ui_set_wind(int fromDeg, const char *text, const char *feels) {
  windDeg = fromDeg;
  snprintf(windStr, sizeof(windStr), "%s", text ? text : "");
  snprintf(feelsStr, sizeof(feelsStr), "%s", feels ? feels : "");
  if (face().onData) face().onData();  // only the active face's hook runs, so no stale pointers are touched
  refreshDetail();
}
