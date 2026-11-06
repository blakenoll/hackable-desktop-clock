#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

static lv_obj_t *timeLabel, *ampmLabel, *dateLabel, *status;

static lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int yOffset) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, "");
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, yOffset);
  return l;
}

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  timeLabel = makeLabel(scr, &lv_font_montserrat_48, lv_color_white(), -16);
  ampmLabel = makeLabel(scr, &lv_font_montserrat_20, lv_color_hex(0x00D0FF), 30);
  dateLabel = makeLabel(scr, &lv_font_montserrat_20, lv_color_hex(0xA0A0A0), 64);

  status = lv_label_create(scr);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_center(status);
  lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
}

// 12-hour clock: "9:05" with AM/PM centred underneath; date as "Thu Nov 06".
void ui_update(const tm &t) {
  static int lastMin = -1, lastDay = -1;
  char buf[24];

  if (t.tm_min != lastMin) {  // redraw the big text only when the minute changes
    lastMin = t.tm_min;
    int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h12, t.tm_min);
    lv_label_set_text(timeLabel, buf);
    lv_label_set_text(ampmLabel, t.tm_hour < 12 ? "AM" : "PM");
  }

  if (t.tm_yday != lastDay) {
    lastDay = t.tm_yday;
    strftime(buf, sizeof(buf), "%a %b %d", &t);
    lv_label_set_text(dateLabel, buf);
  }
}

void ui_set_status(const char *msg) {
  if (msg) {
    lv_label_set_text(status, msg);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_show_setup(const char *apSsid, const char *apPass, const char *qrPayload, const char *ip) {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *qr = lv_qrcode_create(scr);
  lv_qrcode_set_size(qr, 104);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, qrPayload, strlen(qrPayload));
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);  // quiet zone around the code
  lv_obj_set_style_border_width(qr, 5, 0);
  lv_obj_align(qr, LV_ALIGN_CENTER, 0, -38);

  char buf[48];
  auto addLabel = [&](const char *text, lv_color_t color, int y) {
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  };
  addLabel(apSsid, lv_color_white(), 32);
  snprintf(buf, sizeof(buf), "Password: %s", apPass);
  addLabel(buf, lv_color_white(), 52);
  snprintf(buf, sizeof(buf), "then open %s", ip);
  addLabel(buf, lv_color_hex(0x808080), 74);

  lv_screen_load(scr);
}
