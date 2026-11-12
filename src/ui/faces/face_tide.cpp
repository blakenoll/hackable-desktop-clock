#include "../ui_internal.h"

namespace ui {

// Tide chart in the centre: the tide from 6 h ago to 12 h ahead as a filled curve, a dot at "now", the highs
// and lows marked with their times; the current height, rising / falling and the next turn below.
// Data: ui_set_tide() (NOAA high / low predictions, see tide.cpp); the curve between two turns is a half cosine,
// the usual way tide tables are interpolated.
static constexpr int GX0 = 12, GX1 = 228, GY0 = 84, GY1 = 146;  // chart box in screen pixels (no ring: nearly edge to edge)
static constexpr int PAST_H = 6, AHEAD_H = 12;                  // window around now, hours
static constexpr int STEP = 4;                                  // curve vertex every STEP px
static constexpr int NPTS = (GX1 - GX0) / STEP + 1;
static constexpr int COLS = 36, COL_PITCH = (GX1 - GX0) / COLS, COL_W = 3;  // water fill columns
static constexpr int MAX_EXT = 4;                                // highs / lows visible at once
static constexpr int TIDE_MAX = 16;

static lv_obj_t *pastLine, *futLine, *cols[COLS], *nowDot, *extDot[MAX_EXT], *extLbl[MAX_EXT];
static lv_obj_t *heightLabel, *trendLabel, *nextLabel, *stationLabel, *msgLabel;
static lv_point_precise_t pastPts[NPTS + 1], futPts[NPTS + 1];  // must outlive the line objects
static int colPast[COLS];                                        // 1 = column is left of "now" (dimmer)

// Cached data (survives rebuilds).
static time_t turnT[TIDE_MAX];
static float turnH[TIDE_MAX];
static bool turnHigh[TIDE_MAX];
static int turnN = 0;
static char tideUnit = 'F';
static char tideMsg[32] = "LOADING TIDES";
static char stationName[32] = "";
static float waterTemp = NAN;

// ---- Data helpers ----

// Height at epoch `t`: half a cosine between the surrounding turns (flat beyond the first / last).
static float heightAt(double t) {
  if (t <= turnT[0]) return turnH[0];
  for (int i = 0; i + 1 < turnN; i++) {
    if (t > turnT[i + 1]) continue;
    double u = (t - turnT[i]) / (double)(turnT[i + 1] - turnT[i]);
    return (float)((turnH[i] + turnH[i + 1]) / 2 + (turnH[i] - turnH[i + 1]) / 2 * cos(M_PI * u));
  }
  return turnH[turnN - 1];
}

static time_t nowEpoch() {
  tm c = lastTm;
  c.tm_isdst = -1;
  return mktime(&c);
}

static void fmtClock(time_t t, char *out, size_t n, bool ampm) {  // "4:12 PM" (or "4:12" in the chart) or "16:12"
  tm lt;
  localtime_r(&t, &lt);
  if (opt.h24) snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
  else snprintf(out, n, "%d:%02d%s", lt.tm_hour % 12 == 0 ? 12 : lt.tm_hour % 12, lt.tm_min, !ampm ? "" : lt.tm_hour < 12 ? " AM" : " PM");
}

// ---- Drawing ----

static void tidePaint() {
  if (!futLine) return;
  lv_obj_set_style_line_color(futLine, mixDN(0x6FB6FF, nightC(60)), 0);
  lv_obj_set_style_line_color(pastLine, mixDN(0x34506E, nightC(25)), 0);
  for (int i = 0; i < COLS; i++) bgc(cols[i], colPast[i] ? mixDN(0x121C28, nightC(7)) : mixDN(0x1A3550, nightC(13)));
  bgc(nowDot, mixDN(0xFFFFFF, nightC(100)));
  const Theme &T = THEMES[opt.theme];
  for (int i = 0; i < MAX_EXT; i++) {
    bgc(extDot[i], mixDN(0xCFE6FF, nightC(55)));
    tc(extLbl[i], mixDN(T.sub, nightC(55)));
  }
  tc(heightLabel, mixDN(scaleHex(T.digits, 85), nightC(92)));
  tc(trendLabel, mixDN(T.accent, nightC(70)));
  tc(nextLabel, mixDN(T.sub, nightC(69)));
  tc(stationLabel, mixDN(T.colon, nightC(45)));
  tc(msgLabel, mixDN(T.sub, nightC(69)));
}

static void tideUpdate() {
  if (!futLine || !haveTm) return;
  bool have = turnN >= 2;
  setHidden(msgLabel, have);
  for (lv_obj_t *o : {pastLine, futLine, nowDot, heightLabel, trendLabel, nextLabel, stationLabel}) setHidden(o, !have);
  for (int i = 0; i < COLS; i++) setHidden(cols[i], !have);
  for (int i = 0; i < MAX_EXT; i++) { setHidden(extDot[i], true); setHidden(extLbl[i], true); }
  if (!have) {
    setText(msgLabel, tideMsg);
    return;
  }

  time_t now = nowEpoch();
  double t0 = (double)now - PAST_H * 3600.0, span = (PAST_H + AHEAD_H) * 3600.0;  // window start, length (s)
  auto tAtX = [&](float x) { return t0 + (x - GX0) * span / (GX1 - GX0); };
  auto xAtT = [&](double t) { return (float)(GX0 + (t - t0) * (GX1 - GX0) / span); };

  // Vertical scale: the window's range plus a little headroom, so the curve never touches the box edge.
  float vals[NPTS], lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < NPTS; i++) {
    vals[i] = heightAt(tAtX(GX0 + i * STEP));
    lo = fminf(lo, vals[i]);
    hi = fmaxf(hi, vals[i]);
  }
  if (hi - lo < 0.2f) { float m = (hi + lo) / 2; lo = m - 0.1f; hi = m + 0.1f; }
  const float top = GY0 + 4, bot = GY1 - 6;
  auto yOf = [&](float h) { return bot - (h - lo) * (bot - top) / (hi - lo); };

  // Curve, split at now: the past dim, the future bright. Both include the "now" vertex.
  float nowX = xAtT((double)now), nowY = yOf(heightAt((double)now));
  int np = 0, nf = 0;
  for (int i = 0; i < NPTS; i++) {
    float x = GX0 + i * STEP;
    lv_point_precise_t p = {(lv_value_precise_t)x, (lv_value_precise_t)yOf(vals[i])};
    if (x < nowX) pastPts[np++] = p;
    else {
      if (nf == 0) { pastPts[np++] = futPts[nf++] = {(lv_value_precise_t)nowX, (lv_value_precise_t)nowY}; }
      futPts[nf++] = p;
    }
  }
  lv_line_set_points(pastLine, pastPts, np);
  lv_line_set_points(futLine, futPts, nf);

  // Water fill: thin columns from the curve down to the floor of the box.
  for (int i = 0; i < COLS; i++) {
    float cx = GX0 + COL_PITCH * i + COL_PITCH / 2.0f;
    int y = (int)lroundf(yOf(heightAt(tAtX(cx)))) + 4;  // a gap under the line
    int h = GY1 - y;
    setHidden(cols[i], h < 1);
    if (h < 1) continue;
    lv_obj_set_pos(cols[i], (int)lroundf(cx - COL_W / 2.0f), y);
    lv_obj_set_size(cols[i], COL_W, h);
    colPast[i] = cx < nowX;
  }
  lv_obj_set_pos(nowDot, (int)lroundf(nowX) - 5, (int)lroundf(nowY) - 5);

  // Highs and lows in the window: a dot and the time, inside the curve (under a high, over a low).
  int shown = 0, next = -1;
  for (int k = 0; k < turnN; k++) {
    if (next < 0 && turnT[k] > now) next = k;
    float x = xAtT((double)turnT[k]);
    if (x < GX0 + 2 || x > GX1 - 2 || shown >= MAX_EXT) continue;
    float y = yOf(turnH[k]);
    lv_obj_set_pos(extDot[shown], (int)lroundf(x) - 3, (int)lroundf(y) - 3);
    setHidden(extDot[shown], fabsf(x - nowX) < 8);  // the now dot covers it
    char buf[12];
    fmtClock(turnT[k], buf, sizeof(buf), false);  // short: the chart is narrow
    lv_label_set_text(extLbl[shown], buf);
    lv_obj_update_layout(extLbl[shown]);
    int w = lv_obj_get_width(extLbl[shown]);
    int lx = (int)lroundf(x) - w / 2;
    lx = lx < GX0 ? GX0 : lx + w > GX1 ? GX1 - w : lx;
    lv_obj_set_pos(extLbl[shown], lx, turnHigh[k] ? (int)lroundf(y) + 7 : (int)lroundf(y) - 19);
    setHidden(extLbl[shown], false);
    shown++;
  }
  tidePaint();

  // Readout: "+2.4 ft  RISING", then "LOW 10:30 PM".
  float hNow = heightAt((double)now);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f %s", hNow, tideUnit == 'C' ? "m" : "ft");  // above MLLW, like a tide table
  lv_label_set_text(heightLabel, buf);
  lv_label_set_text(trendLabel, next >= 0 ? (turnHigh[next] ? "RISING" : "FALLING") : "");
  // "LOW 3:58 PM · WATER 69°" (either half alone if the other is unknown)
  buf[0] = 0;
  if (next >= 0) {
    char clk[12];
    fmtClock(turnT[next], clk, sizeof(clk), true);
    snprintf(buf, sizeof(buf), "%s %s", turnHigh[next] ? "HIGH" : "LOW", clk);
  }
  if (!isnan(waterTemp)) {
    char w[20];
    snprintf(w, sizeof(w), "%sWATER %d\xC2\xB0", buf[0] ? " \xC2\xB7 " : "", (int)lroundf(waterTemp));
    strlcat(buf, w, sizeof(buf));
  }
  lv_label_set_text(nextLabel, buf);
  lv_label_set_text(stationLabel, stationName);
}

static void build() {
  mkDateRow(-86, 5, 1);
  mkTime(-62, &manrope_34);
  mkOffline(-66, -62);
  for (int i = 0; i < COLS; i++) {
    cols[i] = box(content);
    lv_obj_set_style_radius(cols[i], 1, 0);
    lv_obj_set_style_bg_opa(cols[i], LV_OPA_COVER, 0);
    colPast[i] = 0;
  }
  for (lv_obj_t **l : {&pastLine, &futLine}) {
    *l = lv_line_create(content);
    lv_obj_remove_style_all(*l);
    lv_obj_set_pos(*l, 0, 0);
    lv_obj_set_style_line_width(*l, 3, 0);
    lv_obj_set_style_line_rounded(*l, true, 0);
  }
  for (int i = 0; i < MAX_EXT; i++) {
    extDot[i] = mkDisc(content, 7);
    extLbl[i] = mkLabel(content, &manrope_caps_12);
  }
  nowDot = mkDisc(content, 11);
  lv_obj_set_style_border_width(nowDot, 2, 0);  // black outline lifts it off the line
  lv_obj_set_style_border_color(nowDot, lv_color_black(), 0);
  lv_obj_set_style_border_opa(nowDot, LV_OPA_COVER, 0);
  msgLabel = mkLabelAt(&manrope_caps_12, 1, 0, -5);

  lv_obj_t *row = mkRow(content, 200, 0, 46, 7);
  heightLabel = mkLabel(row, &manrope_20);
  trendLabel = mkLabel(row, &manrope_caps_12, 1);
  nextLabel = mkLabelAt(&manrope_caps_12, 1, 0, 67);
  stationLabel = mkLabelAt(&manrope_caps_12, 2, 0, 87);
  lv_label_set_long_mode(stationLabel, LV_LABEL_LONG_DOT);  // the bezel is ~150 px wide down here
  lv_obj_set_width(stationLabel, 140);
  lv_obj_set_style_text_align(stationLabel, LV_TEXT_ALIGN_CENTER, 0);
}

static void palette(Palette &p, const Theme &) { p.day = p.sub; }
static void paint(const Theme &, const Palette &) { tidePaint(); }
static void onTime(const tm &, bool minuteChanged) { if (minuteChanged) tideUpdate(); }
static void onData() { tideUpdate(); }

static FaceDef make() {
  FaceDef f;
  f.name = "Tide";
  f.ring = K_NONE;  // no seconds ring: the chart gets the full width
  f.build = build;
  f.date = dateQuietLine;
  f.palette = palette;
  f.paint = paint;
  f.onTime = onTime;
  f.onData = onData;
  return f;
}
extern const FaceDef faceDefTide = make();

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

void ui_set_tide(const time_t *t, const float *h, const bool *high, int n, char unit, const char *msg) {
  turnN = 0;
  snprintf(tideMsg, sizeof(tideMsg), "%s", msg ? msg : "");
  tideUnit = unit;
  for (int k = 0; k < n && t && turnN < TIDE_MAX; k++) {
    if (turnN && t[k] <= turnT[turnN - 1]) continue;  // must be ascending
    turnT[turnN] = t[k];
    turnH[turnN] = h[k];
    turnHigh[turnN] = high[k];
    turnN++;
  }
  if (face().onData) face().onData();  // only the active face's hook runs, so no stale pointers are touched
}

void ui_set_tide_info(const char *station, float water) {
  snprintf(stationName, sizeof(stationName), "%s", station ? station : "");
  waterTemp = water;
  if (face().onData) face().onData();
}
