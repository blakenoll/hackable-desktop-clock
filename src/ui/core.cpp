#include "ui_internal.h"

namespace ui {

lv_obj_t *faceRoot, *ringBox, *content;

// ---- Palette: text colours for the current theme and night blend ---------------------------------
Palette makePalette() {
  const Theme &T = THEMES[opt.theme];
  Palette p;
  p.digits = mixDN(T.digits, nightC(100));
  p.accent = mixDN(T.accent, nightC(100));
  p.sub = mixDN(T.sub, nightC(69));
  p.timeH = p.timeC = p.timeM = p.date2 = p.digits;
  p.day = p.accent;  // weekday: the accent
  p.ampm = p.sec = p.date = p.detail = p.sub;
  p.weather = mixDN(scaleHex(T.digits, 85), nightC(92));
  p.icon = mixDN(iconDayColor, nightC(92));
  if (face().palette) face().palette(p, T);
  return p;
}

void applyIconColor() {
  if (weatherIcon) lv_obj_set_style_text_color(weatherIcon, makePalette().icon, 0);
}

// Recolours everything for the current nightMix, theme and night colour.
void applyTheme() {
  bootPaint();
  if (!faceRoot) return;
  const Theme &T = THEMES[opt.theme];
  const FaceDef &f = face();
  Palette p = makePalette();

  tc(timeH, p.timeH);
  tc(timeC, p.timeC);
  tc(timeM, p.timeM);
  tc(ampmLabel, p.ampm);
  tc(secLabel, p.sec);
  tc(dayLabel, p.day);
  tc(dateLabel, p.date);
  tc(date2Label, p.date2);
  weatherPaint(p);
  if (f.paint) f.paint(T, p);

  // Quiet night: the ring fades to black along with everything else, then is hidden (no motion at all).
  // The analog dial's batons stay, dimmed, so the hands can be read.
  bool hideRing = nightMix == 255 && !f.ringAtNight;
  setHidden(ringBox, hideRing);
  if (hideRing) return;
  ringPaint(T);
  if (f.paintRing) f.paintRing(T);
}

// ---- Night: cross-fade between the day and night colours ------------------------------------------
static uint8_t appliedMix = 0;
static void nightExec(void *, int32_t v) {
  nightMix = (uint8_t)v;
  // Restyling 60 ticks + labels every frame is wasteful; steps of 4/255 are invisible.
  if (abs((int)v - (int)appliedMix) >= 4 || v == 0 || v == 255) {
    appliedMix = nightMix;
    applyTheme();
  }
}

// ---- Building and switching ------------------------------------------------------------------------
static void rebuild() {
  ringStop();
  lv_obj_clean(faceRoot);  // deletes every face object (and the animations running on them)
  timeReset();
  weatherReset();
  transitionsReset();

  for (lv_obj_t **o : {&ringBox, &content}) {
    *o = box(faceRoot);
    lv_obj_set_size(*o, 240, 240);
  }
  const FaceDef &f = face();
  ringBuild(f.ring);
  f.build();
  applyTheme();

  // Fill the new face from the cached state.
  if (haveTm) ui_update(lastTm);
  else lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
  if (f.onData) f.onData();
  weatherRestore();
}

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

void ui_set_night(bool n) {
  static bool first = true;
  night = n;
  uint8_t target = n ? 255 : 0;
  lv_anim_delete(&nightMix, nullptr);
  if (first || nightMix == target) {  // boot (or no change): apply instantly
    first = false;
    nightMix = appliedMix = target;
    applyTheme();
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &nightMix);
  lv_anim_set_exec_cb(&a, nightExec);
  lv_anim_set_values(&a, nightMix, target);
  lv_anim_set_duration(&a, (uint32_t)abs((int)target - (int)nightMix) * 45000UL / 255);  // ~45 s for a full switch
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
  bootPaint();  // boot/setup widgets follow the target immediately
}

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  faceRoot = box(scr);
  lv_obj_set_size(faceRoot, 240, 240);
  bootInit(scr);
  rebuild();
}

void ui_set_options(const UiOptions &o) {
  opt = o;
  if (opt.face >= FACE_COUNT) opt.face = 0;
  if (opt.theme >= THEME_COUNT) opt.theme = 0;
  if (opt.nightColor >= NIGHT_COUNT) opt.nightColor = 0;
  opt.dateFmt = opt.dateFmt == 1 ? 1 : 0;
  if (faceRoot) rebuild();
}

void ui_set_theme(int theme) {
  opt.theme = (uint8_t)(theme >= 0 && theme < THEME_COUNT ? theme : 0);
  if (faceRoot) rebuild();
}

int ui_theme_count() { return THEME_COUNT; }
const char *ui_theme_name(int i) { return i >= 0 && i < THEME_COUNT ? THEMES[i].name : ""; }

void ui_set_face(int face) {
  opt.face = (uint8_t)(face >= 0 && face < FACE_COUNT ? face : 0);
  if (faceRoot) rebuild();
}
