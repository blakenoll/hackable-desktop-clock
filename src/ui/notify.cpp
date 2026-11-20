// Notifications: a full-screen card (icon, title, text) laid over whatever face is showing, sent through the
// settings API (POST /api/notify, src/api/api.cpp). One card at a time: a new one replaces the current one.
// It lives on LVGL's top layer, so face rebuilds never touch it, and it is recoloured by applyTheme() like the
// rest of the UI. A timed card shows its remaining time as a ring that unwinds; a card with no time stays until
// it is dismissed (BOOT, or DELETE /api/notify). Alarms ignore night colours and pulse.
#include "ui_internal.h"

namespace ui {

static lv_obj_t *panel, *ring, *iconLbl, *titleLbl, *bodyLbl;
static lv_timer_t *expiry;
static int curLevel;
static char curId[NOTIFY_ID_MAX + 1];

constexpr int RING_SIZE = 228, RING_WIDTH = 6, CONTENT_W = 172;
constexpr uint32_t ALARM_RED = 0xFF3030, ALARM_TEXT = 0xFFE8E8, ALARM_SUB = 0xFFB0B0, WARN_AMBER = 0xFFB020, INFO_GREEN = 0x34C759;

// Copies the text the fonts can draw: printable ASCII and the degree sign. Other characters become '?'
// (one per character, not per byte) and control characters become spaces.
static void clean(char *dst, size_t n, const char *src) {
  size_t o = 0;
  for (const unsigned char *s = (const unsigned char *)(src ? src : ""); *s && o + 3 < n; s++) {
    if (s[0] == 0xC2 && s[1] == 0xB0) {
      dst[o++] = (char)0xC2;
      dst[o++] = (char)0xB0;
      s++;
    } else if (s[0] >= 0x20 && s[0] < 0x7F) {
      dst[o++] = (char)*s;
    } else if (s[0] >= 0xC0) {
      dst[o++] = '?';  // lead byte of a character we have no glyph for
    } else if (s[0] < 0x80) {
      dst[o++] = ' ';
    }  // else: continuation byte of that character, skipped
  }
  dst[o] = 0;
}

static void arcExec(void *o, int32_t v) { lv_arc_set_value((lv_obj_t *)o, v); }
static void opaExec(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

void notifyPaint() {
  if (!panel) return;
  Palette p = makePalette();
  bool alarm = curLevel == NOTIFY_ALARM;
  lv_color_t c = alarm ? lv_color_hex(ALARM_RED) : curLevel == NOTIFY_WARN ? mixDN(WARN_AMBER, nightC(100)) : mixDN(INFO_GREEN, nightC(100));
  lv_obj_set_style_arc_color(ring, c, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, lv_color_mix(c, lv_color_black(), 70), LV_PART_MAIN);
  lv_obj_set_style_text_color(iconLbl, c, 0);
  lv_obj_set_style_text_color(titleLbl, alarm ? lv_color_hex(ALARM_TEXT) : p.digits, 0);
  lv_obj_set_style_text_color(bodyLbl, alarm ? lv_color_hex(ALARM_SUB) : p.sub, 0);
}

static void hide() {
  if (expiry) { lv_timer_delete(expiry); expiry = nullptr; }
  if (panel) { lv_obj_delete(panel); panel = ring = iconLbl = titleLbl = bodyLbl = nullptr; }  // also stops its animations
  curId[0] = 0;
}

static void expired(lv_timer_t *) {
  expiry = nullptr;  // one-shot: LVGL deletes it after this call
  hide();
}

}  // namespace ui

using namespace ui;

void ui_notify(const char *id, const char *title, const char *text, int level, uint32_t durationMs) {
  hide();
  curLevel = level < NOTIFY_INFO || level > NOTIFY_ALARM ? NOTIFY_INFO : level;
  clean(curId, sizeof(curId), id);

  panel = box(lv_layer_top());
  lv_obj_set_size(panel, 240, 240);
  lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

  ring = lv_arc_create(panel);
  lv_obj_remove_style_all(ring);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ring, RING_SIZE, RING_SIZE);
  lv_obj_center(ring);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 1000);
  lv_arc_set_value(ring, 1000);
  for (uint32_t part : {(uint32_t)LV_PART_MAIN, (uint32_t)LV_PART_INDICATOR}) {
    lv_obj_set_style_arc_width(ring, RING_WIDTH, part);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, part);
    lv_obj_set_style_arc_rounded(ring, true, part);
  }

  lv_obj_t *col = mkRow(panel, CONTENT_W, 0, 0, 6, true);
  iconLbl = mkLabel(col, &lv_font_montserrat_14);
  lv_label_set_text(iconLbl, curLevel == NOTIFY_INFO ? LV_SYMBOL_BELL : LV_SYMBOL_WARNING);
  titleLbl = mkLabel(col, &manrope_20);
  lv_obj_set_size(titleLbl, CONTENT_W, 26);
  lv_label_set_long_mode(titleLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(titleLbl, LV_TEXT_ALIGN_CENTER, 0);
  bodyLbl = mkLabel(col, &manrope_14);
  lv_obj_set_width(bodyLbl, CONTENT_W);
  lv_label_set_long_mode(bodyLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(bodyLbl, LV_TEXT_ALIGN_CENTER, 0);
  char buf[NOTIFY_TEXT_MAX + 1];
  clean(buf, sizeof(buf), title);
  lv_label_set_text(titleLbl, buf);
  clean(buf, sizeof(buf), text);
  lv_label_set_text(bodyLbl, buf);
  notifyPaint();

  if (durationMs) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, arcExec);
    lv_anim_set_values(&a, 1000, 0);
    lv_anim_set_duration(&a, durationMs);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
    expiry = lv_timer_create(expired, durationMs, nullptr);
    lv_timer_set_repeat_count(expiry, 1);
  }
  if (curLevel == NOTIFY_ALARM) {  // pulse, so it is noticed from across the room
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, opaExec);
    lv_anim_set_values(&a, LV_OPA_COVER, 70);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_reverse_duration(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
  lv_obj_set_style_opa(panel, LV_OPA_TRANSP, 0);
  setOpa(panel, LV_OPA_COVER, 200);  // fade in
}

bool ui_notify_clear(const char *id) {
  if (!panel) return false;
  if (id && *id && strcmp(id, curId)) return false;
  hide();
  return true;
}

bool ui_notify_active() { return panel != nullptr; }
