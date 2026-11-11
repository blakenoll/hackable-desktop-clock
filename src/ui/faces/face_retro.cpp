#include "../ui_internal.h"

namespace ui {

// Seven-segment LCD with a blocky seconds ring.
static lv_obj_t *seg[4][7], *segColon[2];
static char segText[5] = "    ";  // HHMM, ' ' = blank digit
static constexpr int SEG_W = 30, SEG_H = 56, SEG_T = 6;

static void segPaint() {
  if (!seg[0][0]) return;
  static const uint8_t MAP[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};  // bit 0..6 = segment a..g
  const Theme &T = THEMES[opt.theme];
  lv_color_t on = mixDN(T.tick, nightC(100)), ghost = mixDN(scaleHex(T.tick, 9), 0x000000);
  for (int d = 0; d < 4; d++) {
    uint8_t m = isdigit((unsigned char)segText[d]) ? MAP[segText[d] - '0'] : 0;
    for (int s = 0; s < 7; s++) lv_obj_set_style_bg_color(seg[d][s], (m >> s) & 1 ? on : ghost, 0);
  }
  for (auto c : segColon) lv_obj_set_style_bg_color(c, on, 0);
}

static void buildSegments(int cy) {
  auto rect = [](int x, int y, int w, int h) {
    lv_obj_t *o = box(content);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
  };
  static const int xs[4] = {44, 80, 130, 166};
  int y = cy - SEG_H / 2, half = SEG_H / 2, vl = half - SEG_T / 2 - 3;
  for (int d = 0; d < 4; d++) {
    int x = xs[d], r = x + SEG_W - SEG_T;
    seg[d][0] = rect(x + 4, y, SEG_W - 8, SEG_T);                         // a: top
    seg[d][1] = rect(r, y + 4, SEG_T, vl);                                // b: top right
    seg[d][2] = rect(r, y + half + 2, SEG_T, vl);                         // c: bottom right
    seg[d][3] = rect(x + 4, y + SEG_H - SEG_T, SEG_W - 8, SEG_T);         // d: bottom
    seg[d][4] = rect(x, y + half + 2, SEG_T, vl);                         // e: bottom left
    seg[d][5] = rect(x, y + 4, SEG_T, vl);                                // f: top left
    seg[d][6] = rect(x + 4, y + half - SEG_T / 2, SEG_W - 8, SEG_T);      // g: middle
  }
  segColon[0] = rect(117, y + 15, 6, 6);
  segColon[1] = rect(117, y + SEG_H - 21, 6, 6);
}

static void build() {
  memset(seg, 0, sizeof(seg));
  memset(segColon, 0, sizeof(segColon));
  dayLabel = mkLabelAt(&manrope_14, 2, 0, -62);
  buildSegments(116);
  ampmLabel = mkLabelAt(&manrope_caps_12, 1, 64, 38);
  mkWeather(0, 64, false, 0, &manrope_20, &weather_icons);
  mkDetail(0, 90);
  mkOffline(0, -86);
}

static void date(const DateParts &d) {  // "TUE 11-10" (or day first)
  char line[24];
  if (opt.dateFmt == 1) snprintf(line, sizeof(line), "%s %d-%d", d.wday, d.t.tm_mday, d.t.tm_mon + 1);
  else snprintf(line, sizeof(line), "%s %d-%d", d.wday, d.t.tm_mon + 1, d.t.tm_mday);
  setText(dayLabel, line);
}

static void palette(Palette &p, const Theme &T) {
  lv_color_t lcd = mixDN(T.tick, nightC(100)), lcdDim = mixDN(scaleHex(T.tick, 60), nightC(69));
  p.ampm = p.day = p.detail = lcdDim;
  p.weather = p.icon = lcd;
}

static void paint(const Theme &, const Palette &) { segPaint(); }

static void onTime(const tm &t, bool minuteChanged) {
  if (!minuteChanged) return;
  char hh[8], mm[8];
  fmtHour(t, hh, sizeof(hh));
  snprintf(mm, sizeof(mm), "%02d", t.tm_min);
  snprintf(segText, sizeof(segText), "%2s%s", hh, mm);  // leading hour digit blank in 12-hour mode
  segPaint();
}

static FaceDef make() {
  FaceDef f;
  f.ring = K_BLOCKS;
  f.build = build;
  f.date = date;
  f.palette = palette;
  f.paint = paint;
  f.onTime = onTime;
  return f;
}
extern const FaceDef faceDefRetro = make();

}  // namespace ui
