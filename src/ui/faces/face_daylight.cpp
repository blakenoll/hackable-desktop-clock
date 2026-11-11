#include "../ui_internal.h"

namespace ui {

// 24 h daylight dial around date / time / daylight left / weather: a sunrise-to-sunset arc with a sun dot at "now".
static lv_obj_t *sunLabel, *dayTrack, *dayArc, *sunDot;
static bool sunUp = true;

static void fmtClock(int minutes, char *out, size_t n) {  // "6:42 AM" or "06:42"
  int h = minutes / 60, m = minutes % 60;
  if (opt.h24) snprintf(out, n, "%02d:%02d", h, m);
  else snprintf(out, n, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
}

static void daylightPaint() {
  if (!dayTrack) return;
  lv_obj_set_style_arc_color(dayTrack, mixDN(0x18233D, 0x000000), LV_PART_MAIN);
  lv_obj_set_style_arc_color(dayArc, mixDN(0x5A4520, 0x000000), LV_PART_MAIN);       // daylight still to come
  lv_obj_set_style_arc_color(dayArc, mixDN(0xFFC940, 0x000000), LV_PART_INDICATOR);  // daylight so far
  bgc(sunDot, mixDN(sunUp ? 0xFFE27A : 0xC9D6FF, 0x000000));  // sun by day, pale moon after dark
}

// Daylight dial + the line under the time. Runs each minute and when sunrise/sunset arrive.
static void daylightUpdate() {
  if (!haveTm) return;
  int now = lastTm.tm_hour * 60 + lastTm.tm_min;
  bool have = sunriseMin >= 0 && sunsetMin > sunriseMin;
  sunUp = !have || (now >= sunriseMin && now < sunsetMin);
  if (dayTrack) {
    auto ang = [](int min) { return ((min - 720) / 4 + 360) % 360; };  // 4 minutes per degree, noon on top
    setHidden(dayArc, !have);
    if (have) {
      int upTo = sunUp ? now : sunriseMin;  // after dark the whole span is dim
      lv_arc_set_bg_angles(dayArc, ang(sunriseMin), ang(sunsetMin));
      lv_arc_set_angles(dayArc, ang(sunriseMin), ang(upTo));  // after set_bg_angles, which resets it
    }
    float x, y;
    polar(111, (now - 720) / 4.0f, x, y);
    lv_obj_set_pos(sunDot, (int32_t)lroundf(x - 6.5f), (int32_t)lroundf(y - 6.5f));
    setHidden(sunDot, false);
    daylightPaint();
  }
  if (sunLabel) {
    char buf[32] = "", clk[12];
    if (have && sunUp) {
      int left = sunsetMin - now;
      snprintf(buf, sizeof(buf), "DAYLIGHT %dH %02dM", left / 60, left % 60);
    } else if (have) {
      fmtClock(sunriseMin, clk, sizeof(clk));
      snprintf(buf, sizeof(buf), "SUNRISE %s", clk);
    }
    lv_label_set_text(sunLabel, buf);
  }
}

static void build() {
  // The dial: noon on top, 4 minutes per degree.
  dayTrack = mkRingArc(6);
  lv_obj_set_style_arc_opa(dayTrack, LV_OPA_TRANSP, LV_PART_INDICATOR);
  dayArc = mkRingArc(6);
  lv_obj_add_flag(dayArc, LV_OBJ_FLAG_HIDDEN);
  sunDot = mkDisc(ringBox, 13);
  lv_obj_set_style_border_width(sunDot, 2, 0);  // black outline separates the dot from the arc
  lv_obj_set_style_border_color(sunDot, lv_color_black(), 0);
  lv_obj_set_style_border_opa(sunDot, LV_OPA_COVER, 0);
  lv_obj_add_flag(sunDot, LV_OBJ_FLAG_HIDDEN);

  mkDateRow(-60, 7, 2);
  mkTime(-10, &manrope_56);
  sunLabel = mkLabelAt(&manrope_caps_12, 1, 0, 25);
  mkWeather(0, 56, false, 0, &manrope_20, &weather_icons);
  mkDetail(0, 85);
  mkOffline(0, -84);
}

static void paint(const Theme &, const Palette &p) { tc(sunLabel, p.sub); }
static void paintRing(const Theme &) { daylightPaint(); }
static void onTime(const tm &, bool minuteChanged) { if (minuteChanged) daylightUpdate(); }
static void onData() { daylightUpdate(); }  // sunrise / sunset arrived

static FaceDef make() {
  FaceDef f;
  f.name = "Daylight";
  f.ring = K_CUSTOM;
  f.build = build;
  f.paint = paint;
  f.paintRing = paintRing;
  f.onTime = onTime;
  f.onData = onData;
  return f;
}
extern const FaceDef faceDefDaylight = make();

}  // namespace ui
