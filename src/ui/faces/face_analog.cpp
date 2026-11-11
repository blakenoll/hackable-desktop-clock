#include "../ui_internal.h"

namespace ui {

// Hands on a dial; the date in a window at 3 o'clock, the weather at 6.
static lv_obj_t *winBox;
static lv_obj_t *handH, *handM, *handS, *handCap, *handPin;
static lv_point_precise_t ptsH[2], ptsM[2], ptsS[2];  // must outlive the line objects

static void handSet(lv_obj_t *l, lv_point_precise_t *pts, float deg, float len, float back) {
  float x0, y0, x1, y1;
  polar(-back, deg, x0, y0);
  polar(len, deg, x1, y1);
  pts[0] = {(lv_value_precise_t)lroundf(x0), (lv_value_precise_t)lroundf(y0)};
  pts[1] = {(lv_value_precise_t)lroundf(x1), (lv_value_precise_t)lroundf(y1)};
  lv_line_set_points(l, pts, 2);
}

static void handsUpdate(const tm &t, bool minuteChanged) {
  if (!handH) return;
  if (minuteChanged) {
    handSet(handH, ptsH, (t.tm_hour % 12 + t.tm_min / 60.0f) * 30.0f, 54, 0);
    handSet(handM, ptsM, t.tm_min * 6.0f, 86, 0);
  }
  if (nightMix != 255) handSet(handS, ptsS, t.tm_sec * 6.0f, 98, 22);  // hidden at night: nothing moves
}

static void buildHands() {
  auto hand = [](int w) {
    lv_obj_t *l = lv_line_create(content);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
  };
  handH = hand(7);
  handM = hand(5);
  handS = hand(2);
  handCap = mkDisc(content, 12);
  lv_obj_center(handCap);
  handPin = mkDisc(content, 4);
  lv_obj_center(handPin);
  lv_obj_set_style_bg_color(handPin, lv_color_black(), 0);
  tm zero = {};
  handsUpdate(zero, true);
}

static void build() {
  winBox = mkRow(content, 52, 55, 0, 5);
  lv_obj_set_height(winBox, 24);
  lv_obj_set_style_radius(winBox, 5, 0);
  lv_obj_set_style_border_width(winBox, 1, 0);
  lv_obj_set_style_border_opa(winBox, LV_OPA_COVER, 0);
  dayLabel = mkLabel(winBox, &manrope_caps_12);
  date2Label = mkLabel(winBox, &manrope_14);
  mkWeather(0, 52, false, 0, &manrope_20, &weather_icons);
  mkOffline(0, -54);
  buildHands();  // last, so the hands pass over everything else
}

static void date(const DateParts &d) {
  setText(dayLabel, d.wday);
  setText(date2Label, d.num);
}

static void paint(const Theme &T, const Palette &p) {
  lv_obj_set_style_border_color(winBox, mixDN(T.base, nightC(30)), 0);
  lv_obj_set_style_line_color(handH, p.digits, 0);
  lv_obj_set_style_line_color(handM, p.digits, 0);
  lv_obj_set_style_line_color(handS, mixDN(T.tick, 0x000000), 0);
  bgc(handCap, mixDN(T.tick, nightC(100)));
  setHidden(handS, nightMix == 255);
}

static void onTime(const tm &t, bool minuteChanged) { handsUpdate(t, minuteChanged); }

static FaceDef make() {
  FaceDef f;
  f.name = "Analog";
  f.ring = K_MARKERS;
  f.build = build;
  f.date = date;
  f.paint = paint;
  f.onTime = onTime;
  f.ringAtNight = true;  // the batons stay (dimmed) so the hands can be read
  return f;
}
extern const FaceDef faceDefAnalog = make();

}  // namespace ui
