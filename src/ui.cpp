#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ui.h"

LV_FONT_DECLARE(weather_icons);

static lv_obj_t *spinner, *weatherRow, *weatherIcon, *weatherLabel, *hiloBox, *hiLabel, *loLabel, *timeLabel, *dayLabel, *dateLabel, *status;

// ---- Seconds ring: 60 ticks around the rim --------------------------------------
// Elapsed seconds are softly lit, the current second is bright red, upcoming ones are dim.
static constexpr int TICK_COUNT = 60;
static constexpr float RING_CENTER = 119.5f;   // display is 240x240
static constexpr float TICK_OUTER = 117.0f;    // stay just inside the round bezel
static constexpr float TICK_MINOR_LEN = 7.0f;
static constexpr float TICK_MAJOR_LEN = 12.0f; // every 5th second
static lv_obj_t *ticks[TICK_COUNT];
static bool night = false;
static uint32_t iconDayColor = 0xFFFFFF;
static void applyIconColor() {
  lv_obj_set_style_text_color(weatherIcon, lv_color_hex(night ? 0xC01515 : iconDayColor), 0);
}
static int ringSec = -1;  // last second drawn on the ring (-1 = not drawn yet)
static lv_point_precise_t tickPts[TICK_COUNT][2];  // must outlive the line objects

static void styleTick(int i, int sec) {
  bool major = i % 5 == 0;
  uint32_t color = night ? (i == sec ? 0xFF2020 : (i < sec ? 0x8A1414 : 0x420C0C))
                         : (i == sec ? 0xFF3030 : (i < sec ? 0x8A95A3 : 0x2A3138));
  lv_obj_set_style_line_color(ticks[i], lv_color_hex(color), 0);
  lv_obj_set_style_line_width(ticks[i], (major || i == sec) ? 3 : 2, 0);
}

static void createTicks(lv_obj_t *scr) {
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < TICK_COUNT; i++) {
    float a = i * (2.0f * (float)M_PI / TICK_COUNT);  // clockwise from 12 o'clock
    float sn = sinf(a), cs = cosf(a);
    float rIn = TICK_OUTER - (i % 5 == 0 ? TICK_MAJOR_LEN : TICK_MINOR_LEN);
    float x1 = RING_CENTER + rIn * sn, y1 = RING_CENTER - rIn * cs;
    float x2 = RING_CENTER + TICK_OUTER * sn, y2 = RING_CENTER - TICK_OUTER * cs;
    // lv_line points are relative to the object, which sits at the segment's bounding-box corner.
    float minx = fminf(x1, x2), miny = fminf(y1, y2);
    tickPts[i][0] = {(lv_value_precise_t)lroundf(x1 - minx), (lv_value_precise_t)lroundf(y1 - miny)};
    tickPts[i][1] = {(lv_value_precise_t)lroundf(x2 - minx), (lv_value_precise_t)lroundf(y2 - miny)};
    ticks[i] = lv_line_create(scr);
    lv_obj_remove_style_all(ticks[i]);
    lv_line_set_points(ticks[i], tickPts[i], 2);
    lv_obj_set_pos(ticks[i], (int32_t)lroundf(minx), (int32_t)lroundf(miny));
    lv_obj_set_style_line_rounded(ticks[i], true, 0);
    styleTick(i, -1);
  }
}

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
  createTicks(scr);

  // Weather row: [icon] 72F, centred below the time (kept inside the round bezel).
  weatherRow = lv_obj_create(scr);
  lv_obj_remove_style_all(weatherRow);
  lv_obj_set_size(weatherRow, 190, 36);
  lv_obj_align(weatherRow, LV_ALIGN_CENTER, 0, 58);
  lv_obj_set_flex_flow(weatherRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(weatherRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(weatherRow, 8, 0);
  spinner = lv_spinner_create(weatherRow);
  lv_obj_set_size(spinner, 30, 30);
  lv_spinner_set_anim_params(spinner, 1000, 90);
  lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);  // no track, just the moving arc
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0x8AB4F8), LV_PART_INDICATOR);
  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  weatherIcon = lv_label_create(weatherRow);
  lv_obj_set_style_text_font(weatherIcon, &weather_icons, 0);
  lv_label_set_text(weatherIcon, "");
  weatherLabel = lv_label_create(weatherRow);
  lv_obj_set_style_text_font(weatherLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(weatherLabel, lv_color_hex(0xD0D8E0), 0);
  lv_label_set_text(weatherLabel, "");

  // Small stacked high (top) / low (bottom) to the right of the temperature.
  hiloBox = lv_obj_create(weatherRow);
  lv_obj_remove_style_all(hiloBox);
  lv_obj_set_size(hiloBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hiloBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(hiloBox, 0, 0);
  lv_obj_set_style_pad_left(hiloBox, 4, 0);
  hiLabel = lv_label_create(hiloBox);
  lv_obj_set_style_text_font(hiLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hiLabel, lv_color_hex(0xFF8A65), 0);
  loLabel = lv_label_create(hiloBox);
  lv_obj_set_style_text_font(loLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(loLabel, lv_color_hex(0x6FB6FF), 0);
  lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
  timeLabel = makeLabel(scr, &lv_font_montserrat_48, lv_color_white(), 0);  // dead centre
  // Date row: weekday in red, then "Nov 6" in gray, centred together.
  lv_obj_t *dateRow = lv_obj_create(scr);
  lv_obj_remove_style_all(dateRow);
  lv_obj_set_size(dateRow, 190, LV_SIZE_CONTENT);
  lv_obj_align(dateRow, LV_ALIGN_CENTER, 0, -58);
  lv_obj_set_flex_flow(dateRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dateRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dateRow, 6, 0);
  dayLabel = lv_label_create(dateRow);
  lv_obj_set_style_text_font(dayLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(dayLabel, lv_color_hex(0xFF4D4D), 0);
  lv_label_set_text(dayLabel, "");
  dateLabel = lv_label_create(dateRow);
  lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xA0A0A0), 0);
  lv_label_set_text(dateLabel, "");

  status = lv_label_create(scr);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_center(status);
  lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
}

// Layout: seconds ring on the rim; date on top, time dead centre, weather (icon, temp, stacked high/low) below.
void ui_update(const tm &t) {
  static int lastMin = -1, lastDay = -1;
  char buf[24];

  if (t.tm_sec != ringSec) {  // seconds ring
    if (ringSec < 0 || t.tm_sec < ringSec) {  // first draw or new minute: restyle everything
      for (int i = 0; i < TICK_COUNT; i++) styleTick(i, t.tm_sec);
    } else {
      for (int i = ringSec; i <= t.tm_sec; i++) styleTick(i, t.tm_sec);  // covers skipped seconds too
    }
    ringSec = t.tm_sec;
  }

  if (t.tm_min != lastMin) {  // redraw the big text only when the minute changes
    lastMin = t.tm_min;
    int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h12, t.tm_min);
    lv_label_set_text(timeLabel, buf);
  }

  if (t.tm_yday != lastDay) {
    lastDay = t.tm_yday;
    char wday[8], mon[8];
    strftime(wday, sizeof(wday), "%a", &t);
    strftime(mon, sizeof(mon), "%b", &t);
    lv_label_set_text(dayLabel, wday);
    snprintf(buf, sizeof(buf), "%s %d", mon, t.tm_mday);  // %d would zero-pad single digits
    lv_label_set_text(dateLabel, buf);
  }
}

void ui_set_night(bool n) {
  night = n;
  lv_obj_set_style_text_color(timeLabel, lv_color_hex(n ? 0xD01818 : 0xFFFFFF), 0);
  lv_obj_set_style_text_color(dayLabel, lv_color_hex(n ? 0xD01818 : 0xFF4D4D), 0);   // weekday: brighter
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(n ? 0x901010 : 0xA0A0A0), 0);
  lv_obj_set_style_text_color(weatherLabel, lv_color_hex(n ? 0xC01515 : 0xD0D8E0), 0);
  lv_obj_set_style_text_color(hiLabel, lv_color_hex(n ? 0xC01515 : 0xFF8A65), 0);
  lv_obj_set_style_text_color(loLabel, lv_color_hex(n ? 0x801010 : 0x6FB6FF), 0);  // dimmer than the high
  lv_obj_set_style_text_color(status, lv_color_hex(n ? 0xC01515 : 0xFFFFFF), 0);
  applyIconColor();
  lv_obj_set_style_arc_color(spinner, lv_color_hex(n ? 0xC01515 : 0x8AB4F8), LV_PART_INDICATOR);
  for (int i = 0; i < TICK_COUNT; i++) styleTick(i, ringSec);
}

void ui_set_weather_loading(bool loading) {
  if (loading) {
    lv_obj_add_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(weatherLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, const char *hi, const char *lo) {
  lv_label_set_text(weatherLabel, text ? text : "");
  lv_obj_remove_flag(weatherLabel, LV_OBJ_FLAG_HIDDEN);  // may have been hidden by the spinner
  if (hi && lo) {
    lv_label_set_text(hiLabel, hi);
    lv_label_set_text(loLabel, lo);
    lv_obj_remove_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
  }
  if (icon) {
    lv_label_set_text(weatherIcon, icon);
    iconDayColor = iconColor;
    applyIconColor();
    lv_obj_remove_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN);
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
