#include <lvgl.h>
#include <stdio.h>
#include "ui.h"

// Scale runs 0..60 for hour, minute and second hands alike.
static lv_obj_t *scale, *hourHand, *minHand, *secHand, *dateLabel, *status;

static lv_obj_t *makeHand(lv_color_t color, int width) {
  lv_obj_t *l = lv_line_create(scale);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_color(l, color, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return l;
}

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  scale = lv_scale_create(scr);
  lv_obj_set_size(scale, 232, 232);
  lv_obj_center(scale);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_range(scale, 0, 60);
  lv_scale_set_total_tick_count(scale, 61);
  lv_scale_set_major_tick_every(scale, 5);
  lv_scale_set_angle_range(scale, 360);
  lv_scale_set_rotation(scale, 270);
  lv_scale_set_label_show(scale, false);
  lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scale, 0, 0);

  lv_obj_set_style_length(scale, 6, LV_PART_ITEMS);
  lv_obj_set_style_line_width(scale, 1, LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x606060), LV_PART_ITEMS);
  lv_obj_set_style_length(scale, 14, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(scale, 3, LV_PART_INDICATOR);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);

  dateLabel = lv_label_create(scr);
  lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xA0A0A0), 0);
  lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 45);

  hourHand = makeHand(lv_color_white(), 6);
  minHand = makeHand(lv_color_hex(0x00D0FF), 4);
  secHand = makeHand(lv_color_hex(0xFF3030), 2);

  status = lv_label_create(scr);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_center(status);
  lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
}

void ui_update(const tm &t) {
  lv_scale_set_line_needle_value(scale, hourHand, 60, (t.tm_hour % 12) * 5 + t.tm_min / 12);
  lv_scale_set_line_needle_value(scale, minHand, 90, t.tm_min);
  lv_scale_set_line_needle_value(scale, secHand, 100, t.tm_sec);

  char buf[24];
  strftime(buf, sizeof(buf), "%a %b %d", &t);
  lv_label_set_text(dateLabel, buf);
}

void ui_set_status(const char *msg) {
  if (msg) {
    lv_label_set_text(status, msg);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
  }
}
