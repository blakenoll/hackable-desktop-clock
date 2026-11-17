#include "../ui_internal.h"

namespace ui {

// Hours over minutes; the date in a column on the left, the weather on the right.
//
// manrope_74 with tabular digits: every digit takes the same cell (the widest digit's advance) and is centred in it,
// so the hour digits sit exactly over the minute digits whatever the numbers are ("11" over "47" included).
static lv_font_t tabular74;
static int32_t digitCell;

static bool tabularGlyph(const lv_font_t *f, lv_font_glyph_dsc_t *g, uint32_t letter, uint32_t next) {
  if (!manrope_74.get_glyph_dsc(f, g, letter, next)) return false;
  if (letter >= '0' && letter <= '9') {  // replaces adv_w, so kerning between digits is dropped too
    g->ofs_x += (digitCell - g->adv_w) / 2;
    g->adv_w = digitCell;
  }
  return true;
}

static const lv_font_t *tabularFont() {
  if (!digitCell) {
    for (uint32_t c = '0'; c <= '9'; c++) digitCell = LV_MAX(digitCell, lv_font_get_glyph_width(&manrope_74, c, 0));
    tabular74 = manrope_74;
    tabular74.get_glyph_dsc = tabularGlyph;
  }
  return &tabular74;
}

static void build() {
  const lv_font_t *font = tabularFont();
  timeH = mkLabelAt(font, 0, 0, -33);
  timeM = mkLabelAt(font, 0, 0, 33);
  // Both rows are two digit cells wide and centred, so the columns line up; a single-digit hour right-justifies
  // over the minutes' units digit.
  lv_obj_set_width(timeH, 2 * digitCell);
  lv_obj_set_style_text_align(timeH, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(timeH, LV_ALIGN_CENTER, 0, -33);
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

static FaceDef make() {
  FaceDef f;
  f.name = "Stacked";
  f.ring = K_TICKS;
  f.build = build;
  f.date = date;
  f.palette = palette;
  f.msgInDetail = true;  // "ZIP not found" does not fit the side column
  return f;
}
extern const FaceDef faceDefStacked = make();

}  // namespace ui
