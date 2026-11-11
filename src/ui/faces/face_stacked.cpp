#include "../ui_internal.h"

namespace ui {

// Hours over minutes; the date in a column on the left, the weather on the right.
static void build() {
  timeH = mkLabelAt(&manrope_74, 0, 0, -33);
  timeM = mkLabelAt(&manrope_74, 0, 0, 33);
  {  // right-justify the hour over the minutes' units digit (matters for single-digit hours)
    lv_obj_set_width(timeH, 2 * lv_font_get_glyph_width(&manrope_74, 0x30, 0));
    lv_obj_set_style_text_align(timeH, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(timeH, 8, 0);  // "1" is narrow, so the hour pair read tighter than the minutes
    lv_obj_align(timeH, LV_ALIGN_CENTER, 0, -33);
  }
  dayLabel = mkLabelAt(&manrope_caps_12, 2, -77, -21);
  date2Label = mkLabelAt(&manrope_20, 0, -77, 0);
  dateLabel = mkLabelAt(&manrope_caps_12, 2, -77, 21);
  mkWeather(77, -2, true, 0, &manrope_20, &weather_icons);
  ampmLabel = mkLabelAt(&manrope_caps_12, 2, 0, -90);
  if (opt.seconds) secLabel = mkLabelAt(&manrope_14, 0, 77, 40);
  mkDetail(0, 90);
  mkOffline(-77, -46);
}

static void date(const DateParts &d) {  // TUE / 10 / NOV down the left side
  setText(dayLabel, d.wday);
  setText(date2Label, d.num);
  setText(dateLabel, d.mon);
}

static void palette(Palette &p, const Theme &) { p.timeM = p.accent; }

// Two-digit hours centre over the minutes, a single digit right-justifies over their units digit.
static void onText(lv_obj_t *l) {
  if (l != timeH) return;
  lv_obj_set_style_text_align(l, strlen(lv_label_get_text(l)) > 1 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, 0);
}

static FaceDef make() {
  FaceDef f;
  f.ring = K_TICKS;
  f.build = build;
  f.date = date;
  f.palette = palette;
  f.onText = onText;
  f.msgInDetail = true;  // "ZIP not found" does not fit the side column
  return f;
}
extern const FaceDef faceDefStacked = make();

}  // namespace ui
