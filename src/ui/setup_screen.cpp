// The setup-mode screen: which WiFi to join, its password and the address to open (src/setup/portal.cpp calls
// it). A separate LVGL screen, deliberately unaffected by themes and night mode.
#include "ui_internal.h"

using namespace ui;

void ui_show_setup(const char *apSsid, const char *apPass, const char *ip) {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  const lv_color_t dim = lv_color_hex(0x808080);
  auto addLabel = [&](const char *text, const lv_font_t *font, lv_color_t color, int y) {
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  };
  char buf[48];
  addLabel("Join this WiFi", &manrope_14, dim, -54);
  addLabel(apSsid, &manrope_20, lv_color_white(), -28);
  addLabel("Password", &manrope_14, dim, 8);
  addLabel(apPass, &manrope_20, lv_color_white(), 34);
  snprintf(buf, sizeof(buf), "then open %s", ip);
  addLabel(buf, &manrope_14, dim, 72);

  lv_screen_load(scr);
}
