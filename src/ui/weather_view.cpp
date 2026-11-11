#include "ui_internal.h"

namespace ui {

lv_obj_t *weatherRow, *weatherIcon, *weatherLabel, *skeleton, *skelShape[5];
lv_obj_t *hiloBox, *hiLabel, *loLabel, *rangeTrack, *rangeDot;
lv_obj_t *detailBox, *detailLabel, *offBox, *offIcon, *offSlash;
static constexpr int DROP_COUNT = 4;
static lv_obj_t *drops[DROP_COUNT];

// Cached model, so a rebuilt face can be filled in again.
static struct {
  bool set = false, hasIcon = false, hasRange = false;
  char icon[8] = "", text[32] = "";
  uint32_t iconColor = 0xFFFFFF;
  int lo = 0, hi = 0, fall = 0;
  float cur = 0;
} wx;
static char detailText[32] = "";
static bool loadingShown = false, staleShown = false, offShown = false;
int rainPct[RAIN_BARS] = {-1, -1, -1, -1, -1, -1};
int tempHr[RAIN_BARS] = {-32768, -32768, -32768, -32768, -32768, -32768};
int rainStart = -1;
int sunriseMin = -1, sunsetMin = -1;

// ---- Building the weather pieces ---------------------------------------------------------------
// Vertical temperature range bar next to the temperature: high number at the top, low at the bottom.
static constexpr int RANGE_H = 34, RANGE_W = 4, RANGE_DOT = 10;

// Weather block: [icon] [temperature] and optionally today's range (hilo 1: bar with a dot at the
// current temperature + high/low numbers; 2: numbers only). column stacks icon over temperature.
void mkWeather(int x, int y, bool column, int hilo, const lv_font_t *tempFont, const lv_font_t *iconFont) {
  weatherRow = mkRow(content, column ? 70 : 190, x, y, column ? 0 : 8, column);
  // Skeleton placeholder shown while the first fetch is in flight; roughly the size of the real
  // content so nothing shifts when it arrives.
  skeleton = mkRow(weatherRow, LV_SIZE_CONTENT, 0, 0, column ? 6 : 8, column);
  auto shape = [&](lv_obj_t *parent, int w, int h, int radius) {
    lv_obj_t *o = box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
  };
  skelShape[0] = shape(skeleton, 24, 24, LV_RADIUS_CIRCLE);
  skelShape[1] = shape(skeleton, column ? 40 : 56, 16, 8);
  if (hilo == 1) {  // mirrors the range bar: a thin vertical bar with a number bar at its top and bottom
    lv_obj_t *skelHilo = mkRow(skeleton, LV_SIZE_CONTENT, 0, 0, 5);
    lv_obj_set_style_pad_left(skelHilo, 4, 0);
    skelShape[2] = shape(skelHilo, RANGE_W, RANGE_H, RANGE_W / 2);
    lv_obj_t *skelNums = box(skelHilo);
    lv_obj_set_size(skelNums, 20, RANGE_H);
    skelShape[3] = shape(skelNums, 20, 8, 4);
    lv_obj_align(skelShape[3], LV_ALIGN_TOP_LEFT, 0, -1);
    skelShape[4] = shape(skelNums, 20, 8, 4);
    lv_obj_align(skelShape[4], LV_ALIGN_BOTTOM_LEFT, 0, 1);
  }
  lv_anim_t pulse;
  lv_anim_init(&pulse);
  lv_anim_set_var(&pulse, skeleton);
  lv_anim_set_exec_cb(&pulse, [](void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); });
  lv_anim_set_values(&pulse, 70, 255);
  lv_anim_set_duration(&pulse, 900);
  lv_anim_set_reverse_duration(&pulse, 900);
  lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
  lv_anim_start(&pulse);
  lv_obj_add_flag(skeleton, LV_OBJ_FLAG_HIDDEN);

  weatherIcon = mkLabel(weatherRow, iconFont);
  lv_obj_add_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN);  // labels start hidden so the first content fades in
  weatherLabel = mkLabel(weatherRow, tempFont);
  lv_obj_set_user_data(weatherLabel, (void *)tempFont);  // messages ("ZIP not found") switch to a text font
  lv_obj_add_flag(weatherLabel, LV_OBJ_FLAG_HIDDEN);

  if (hilo) {
    hiloBox = mkRow(weatherRow, LV_SIZE_CONTENT, 0, 0, 5);
    lv_obj_set_style_pad_left(hiloBox, hilo == 1 ? 4 : 0, 0);
    if (hilo == 1) {
      rangeTrack = box(hiloBox);
      lv_obj_set_size(rangeTrack, RANGE_W, RANGE_H);
      lv_obj_set_style_radius(rangeTrack, 2, 0);
      lv_obj_set_style_bg_opa(rangeTrack, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_grad_dir(rangeTrack, LV_GRAD_DIR_VER, 0);
      lv_obj_add_flag(rangeTrack, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // the dot is wider than the track
      rangeDot = mkDisc(rangeTrack, RANGE_DOT);
      lv_obj_set_style_border_width(rangeDot, 2, 0);  // black outline separates the dot from the gradient bar
      lv_obj_set_style_border_color(rangeDot, lv_color_black(), 0);
      lv_obj_set_style_border_opa(rangeDot, LV_OPA_COVER, 0);
    }
    lv_obj_t *nums = box(hiloBox);  // high pinned to the top, low to the bottom of the bar's height
    lv_obj_set_size(nums, 26, hilo == 1 ? RANGE_H : 30);
    hiLabel = mkLabel(nums, hilo == 1 ? &manrope_14 : &manrope_caps_12);
    lv_obj_align(hiLabel, LV_ALIGN_TOP_LEFT, 0, hilo == 1 ? -3 : -1);
    loLabel = mkLabel(nums, hilo == 1 ? &manrope_14 : &manrope_caps_12);
    lv_obj_align(loLabel, LV_ALIGN_BOTTOM_LEFT, 0, hilo == 1 ? 3 : 1);
    lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
  }

  // Rain/snow particles: children of the icon, so they follow its fades and the row's night slide.
  lv_obj_add_flag(weatherRow, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(weatherIcon, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  for (auto &d : drops) {
    d = box(weatherIcon);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
  }
}

// Detail line ("40% RAIN" / "FEELS 65°"). The box carries the stale dimming, the label its own swap fades.
void mkDetail(int x, int y) {
  detailBox = box(content);
  lv_obj_set_size(detailBox, 130, 18);
  lv_obj_align(detailBox, LV_ALIGN_CENTER, x, y);
  detailLabel = mkLabel(detailBox, &manrope_caps_12, 1);
  lv_obj_center(detailLabel);
}

// Offline marker: WiFi glyph with a slash through it.
void mkOffline(int x, int y) {
  offBox = box(content);
  lv_obj_set_size(offBox, 20, 16);
  lv_obj_align(offBox, LV_ALIGN_CENTER, x, y);
  offIcon = mkLabel(offBox, &lv_font_montserrat_14);
  lv_label_set_text(offIcon, LV_SYMBOL_WIFI);
  lv_obj_center(offIcon);
  static lv_point_precise_t slashPts[2] = {{2, 1}, {18, 15}};
  offSlash = lv_line_create(offBox);
  lv_obj_remove_style_all(offSlash);
  lv_line_set_points(offSlash, slashPts, 2);
  lv_obj_set_style_line_width(offSlash, 2, 0);
  lv_obj_set_style_line_rounded(offSlash, true, 0);
  lv_obj_set_style_opa(offBox, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(offBox, LV_OBJ_FLAG_HIDDEN);
}

// ---- Range dot ---------------------------------------------------------------------------
// ---- Range dot ---------------------------------------------------------------------------
static int32_t rangeFrac = 500;  // 0..1000 = current temp's position between today's low (0) and high (1000)

static void rangePlace() {
  if (!rangeDot) return;
  float f = rangeFrac / 1000.0f;
  lv_obj_set_pos(rangeDot, -(RANGE_DOT - RANGE_W) / 2, (int)lroundf((1.0f - f) * (RANGE_H - RANGE_DOT)));
}

static void rangeExec(void *, int32_t v) {
  rangeFrac = v;
  rangePlace();
}

static int rangeSlot = 0;
static void rangeNightSync() {
  if (!hiloBox) return;
  bool wanted = wx.set && wx.hasRange;
  if (wanted && !lv_obj_has_flag(hiloBox, LV_OBJ_FLAG_HIDDEN) && rangeSlot == 0) {
    lv_obj_update_layout(weatherRow);
    rangeSlot = lv_obj_get_width(hiloBox) + 8;  // + the row's column gap
  }
  lv_obj_set_style_translate_x(weatherRow, wanted ? rangeSlot * nightMix / 510 : 0, 0);
}

// ---- Rain/snow: a few particles drift down under the icon for a few seconds, then stop ----
// ---- Rain/snow: a few particles drift down under the icon for a few seconds, then stop ----
static int fallKind;  // 1 rain, 2 snow

static constexpr int FALL_DIST = 10;

static void fallExec(void *o, int32_t v) {  // v: 0..1000 through one fall
  lv_obj_t *d = (lv_obj_t *)o;
  int opa = v < 200 ? v * 200 / 200 : (1000 - v) * 200 / 800;  // quick fade in, long fade out (max ~80%)
  lv_obj_set_style_opa(d, (lv_opa_t)opa, 0);
  lv_obj_set_style_translate_y(d, v * FALL_DIST / 1000, 0);
  if (fallKind == 2) lv_obj_set_style_translate_x(d, (int32_t)lroundf(2.0f * sinf(v * 0.00628f)), 0);  // snow sways
}

static void fallStart(lv_anim_t *a) {  // place when it starts, after any icon swap has settled
  lv_obj_t *d = (lv_obj_t *)a->var;
  int k = (int)(intptr_t)lv_anim_get_user_data(a);
  lv_obj_update_layout(weatherRow);
  int w = lv_obj_get_width(weatherIcon), h = lv_obj_get_height(weatherIcon);
  static const int xs[DROP_COUNT] = {30, 46, 62, 78};  // percent of the icon width
  lv_obj_set_pos(d, w * xs[k] / 100 - lv_obj_get_width(d) / 2, h * 62 / 100);
  lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);
}

static void fallDone(lv_anim_t *a) { lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN); }

static void startFall(int kind, uint32_t color) {
  if (kind == 0 || nightMix != 0 || !weatherIcon) return;  // quiet night: nothing moves
  fallKind = kind;
  bool snow = kind == 2;
  for (int k = 0; k < DROP_COUNT; k++) {
    lv_obj_t *d = drops[k];
    lv_anim_delete(d, nullptr);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(d, snow ? 3 : 2, snow ? 3 : 5);
    lv_obj_set_style_radius(d, snow ? LV_RADIUS_CIRCLE : 1, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_opa(d, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_x(d, 0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, d);
    lv_anim_set_exec_cb(&a, fallExec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, snow ? 1800 : 700);
    lv_anim_set_repeat_count(&a, snow ? 2 : 5);  // ~4 s either way
    lv_anim_set_delay(&a, 500 + k * (snow ? 450 : 170) + (k % 2) * (snow ? 200 : 60));  // after the icon fade-in
    lv_anim_set_path_cb(&a, snow ? lv_anim_path_linear : lv_anim_path_ease_in);
    lv_anim_set_user_data(&a, (void *)(intptr_t)k);
    lv_anim_set_start_cb(&a, fallStart);
    lv_anim_set_completed_cb(&a, fallDone);
    lv_anim_start(&a);
  }
}

// The detail line shows the weather message (see above), else the detail text if that option is on.
// The Forecast face adds the wind readout to it ("NW 12 MPH · 40% RAIN").
void refreshDetail() {
  char buf[56] = "";
  if (wx.set && !wx.hasIcon && wx.text[0] && face().msgInDetail) strlcpy(buf, wx.text, sizeof(buf));
  else {
    const char *prefix = face().detailPrefix ? face().detailPrefix() : nullptr;  // Forecast: the wind readout
    bool wind = prefix && prefix[0];
    if (wind) strlcpy(buf, prefix, sizeof(buf));
    if (opt.detail && detailText[0]) {
      if (wind) strlcat(buf, " \xC2\xB7 ", sizeof(buf));
      strlcat(buf, detailText, sizeof(buf));
    }
  }
  upper(buf);  // the small-caps font has no lowercase
  swapText(SW_DETAIL, detailLabel, buf);
}

static void applyStale(uint32_t ms) {
  lv_opa_t to = staleShown ? 90 : LV_OPA_COVER;  // ~35%: still readable, clearly not current
  setOpa(weatherRow, to, ms);
  setOpa(detailBox, to, ms);
  if (face().onStale) face().onStale(to, ms);
}

// Shows the cached weather. Updates fade (out, swap, in) instead of snapping; content appearing after
// the skeleton fades in. The range dot glides to its new position.
static void applyWeather(bool withFall) {
  if (!weatherRow || !wx.set) return;
  if (wx.hasIcon && withFall) startFall(wx.fall, wx.iconColor);
  // Temperatures use the face's own font; a message needs a full text font (and may go to the detail line).
  bool msg = !wx.hasIcon;
  lv_obj_set_style_text_font(weatherLabel, msg ? &manrope_14 : (const lv_font_t *)lv_obj_get_user_data(weatherLabel), 0);
  swapText(SW_TEMP, weatherLabel, msg && face().msgInDetail ? "" : wx.text);
  refreshDetail();
  if (hiloBox && wx.hasRange) {
    char h[12], l[12];
    bool deg = !rangeTrack;  // numbers-only style carries its own degree signs
    snprintf(h, sizeof(h), deg ? "%d\xC2\xB0" : "%d", wx.hi);
    snprintf(l, sizeof(l), deg ? "%d\xC2\xB0" : "%d", wx.lo);
    float f = wx.hi > wx.lo ? (wx.cur - wx.lo) / (float)(wx.hi - wx.lo) : 0.5f;
    int32_t target = (int32_t)lroundf(fminf(fmaxf(f, 0.0f), 1.0f) * 1000);
    lv_anim_delete(&rangeFrac, nullptr);
    if (lv_obj_has_flag(hiloBox, LV_OBJ_FLAG_HIDDEN)) {  // first appearance: place, then fade in
      lv_label_set_text(hiLabel, h);
      lv_label_set_text(loLabel, l);
      rangeFrac = target;
      rangePlace();
      lv_obj_remove_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
      fadeIn(hiloBox);
    } else {
      swapText(SW_HI, hiLabel, h);
      swapText(SW_LO, loLabel, l);
      if (target != rangeFrac) {
        lv_anim_t g;
        lv_anim_init(&g);
        lv_anim_set_var(&g, &rangeFrac);
        lv_anim_set_exec_cb(&g, rangeExec);
        lv_anim_set_values(&g, rangeFrac, target);
        lv_anim_set_duration(&g, 700);
        lv_anim_set_path_cb(&g, lv_anim_path_ease_in_out);
        lv_anim_start(&g);
      }
    }
  } else if (hiloBox) {
    lv_anim_delete(hiloBox, nullptr);
    lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
  }
  rangeNightSync();
  if (wx.hasIcon) {
    pendingIconColor = wx.iconColor;
    if (!lv_obj_has_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN) && !strcmp(lv_label_get_text(weatherIcon), wx.icon)) {
      iconDayColor = wx.iconColor;  // same glyph, maybe a new colour: no fade needed
      applyIconColor();
    } else {
      swapText(SW_ICON, weatherIcon, wx.icon);
    }
  } else {
    lv_anim_delete(weatherIcon, nullptr);
    lv_obj_add_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN);
  }
}

void weatherPaint(const Palette &p) {
  tc(weatherLabel, p.weather);
  tc(detailLabel, p.detail);
  tc(offIcon, p.sub);
  if (offSlash) lv_obj_set_style_line_color(offSlash, p.sub, 0);
  applyIconColor();

  // Quiet night face: the range bar and high/low fade to black (they keep their layout slot).
  tc(hiLabel, mixDN(0xFF8A65, 0x000000));
  tc(loLabel, mixDN(0x6FB6FF, 0x000000));
  if (rangeTrack) {
    lv_obj_set_style_bg_color(rangeTrack, mixDN(0xFF8A65, 0x000000), 0);       // top = high
    lv_obj_set_style_bg_grad_color(rangeTrack, mixDN(0x6FB6FF, 0x000000), 0);  // bottom = low
    lv_obj_set_style_bg_color(rangeDot, mixDN(0xFFFFFF, 0x000000), 0);
  }
  rangeNightSync();
  // The range part of the skeleton fades to black like the real bar, and the skeleton slides to stay centred.
  for (int k = 0; k < 5; k++) bgc(skelShape[k], mixDN(0x4A4F57, k >= 2 ? 0x000000 : nightC(35)));
  if (skeleton && skelShape[2]) {
    constexpr int skelSlot = 4 + RANGE_W + 5 + 20 + 8;  // pad + bar + gap + number bars + the row's gap
    lv_obj_set_style_translate_x(skeleton, skelSlot * nightMix / 510, 0);
  }
}

void weatherReset() {
  lv_anim_delete(&rangeFrac, nullptr);
  weatherRow = weatherIcon = weatherLabel = skeleton = hiloBox = hiLabel = loLabel = rangeTrack = rangeDot = nullptr;
  detailBox = detailLabel = offBox = offIcon = offSlash = nullptr;
  memset(skelShape, 0, sizeof(skelShape));
  memset(drops, 0, sizeof(drops));
  rangeSlot = 0;
}

void weatherRestore() {
  ui_set_weather_loading(loadingShown);
  if (!loadingShown) applyWeather(false);
  refreshDetail();
  applyStale(0);
  if (offBox && offShown) {
    lv_obj_remove_flag(offBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(offBox, LV_OPA_COVER, 0);
  }
}

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

// ---- Weather -------------------------------------------------------------------------------------
void ui_set_weather_loading(bool loading) {
  loadingShown = loading;
  if (!weatherRow) return;
  if (loading) {
    for (lv_obj_t *o : {weatherIcon, weatherLabel, hiloBox}) {
      if (!o) continue;
      lv_anim_delete(o, nullptr);  // drop any fade in flight so the object is clean when it reappears
      lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(skeleton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(skeleton, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_set_weather_detail(const char *text) {
  strlcpy(detailText, text ? text : "", sizeof(detailText));
  refreshDetail();
}

void ui_set_weather_stale(bool stale) {
  if (stale == staleShown) return;
  staleShown = stale;
  applyStale(800);
}

void ui_set_offline(bool off) {
  if (off == offShown) return;
  offShown = off;
  if (!offBox) return;
  lv_obj_remove_flag(offBox, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(offBox, nullptr);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, offBox);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t x) { lv_obj_set_style_opa((lv_obj_t *)v, (lv_opa_t)x, 0); });
  lv_anim_set_values(&a, lv_obj_get_style_opa(offBox, LV_PART_MAIN), off ? LV_OPA_COVER : LV_OPA_TRANSP);
  lv_anim_set_duration(&a, 600);
  if (!off) lv_anim_set_completed_cb(&a, [](lv_anim_t *x) { lv_obj_add_flag((lv_obj_t *)x->var, LV_OBJ_FLAG_HIDDEN); });
  lv_anim_start(&a);
}

void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, bool hasRange, int lo, int hi, float cur,
                    int fall) {
  wx.set = true;
  wx.hasIcon = icon != nullptr;
  strlcpy(wx.icon, icon ? icon : "", sizeof(wx.icon));
  wx.iconColor = iconColor;
  strlcpy(wx.text, text ? text : "", sizeof(wx.text));
  wx.hasRange = hasRange;
  wx.lo = lo;
  wx.hi = hi;
  wx.cur = cur;
  wx.fall = fall;
  applyWeather(true);
}

void ui_set_weather_extra(const int *rain, int count, int startHour, int sunrise, int sunset, const int *temps) {
  for (int k = 0; k < RAIN_BARS; k++) rainPct[k] = rain && k < count ? rain[k] : -1;
  for (int k = 0; k < RAIN_BARS; k++) tempHr[k] = temps && k < count ? temps[k] : -32768;
  rainStart = rain && count > 0 ? startHour : -1;
  sunriseMin = sunrise;
  sunsetMin = sunset;
  if (face().onData) face().onData();
}
