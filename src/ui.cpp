#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ui.h"

LV_FONT_DECLARE(weather_icons);
LV_FONT_DECLARE(manrope_14);
LV_FONT_DECLARE(manrope_20);
LV_FONT_DECLARE(manrope_48);

static lv_obj_t *skeleton, *skelShape[4], *weatherRow, *weatherIcon, *weatherLabel, *hiloBox, *hiLabel, *loLabel, *timeLabel, *dayLabel, *dateLabel, *status;


// ---- Seconds ring: 60 ticks around the rim --------------------------------------
// Elapsed seconds are softly lit, the current second is bright red, upcoming ones are dim.
static constexpr int TICK_COUNT = 60;
static constexpr float RING_CENTER = 119.5f;   // display is 240x240
static constexpr float TICK_OUTER = 117.0f;    // stay just inside the round bezel
static constexpr float TICK_MINOR_LEN = 7.0f;
static constexpr float TICK_MAJOR_LEN = 12.0f; // every 5th second
static lv_obj_t *ticks[TICK_COUNT];
static bool night = false;

// ---- "Connecting" animation: WiFi glyph whose arcs light up in sequence ----------------
static lv_obj_t *wifiBox, *wifiDot, *wifiArc[3];
static lv_timer_t *wifiTimer;
static int wifiStep;

static void wifiPaint() {
  uint32_t on = night ? 0xC01515 : 0xFFFFFF, off = night ? 0x2A0707 : 0x303030;
  // wifiStep 0..3: dot only, +inner arc, +middle arc, +outer arc (then loops)
  lv_obj_set_style_bg_color(wifiDot, lv_color_hex(on), 0);
  for (int i = 0; i < 3; i++)
    lv_obj_set_style_arc_color(wifiArc[i], lv_color_hex(i < wifiStep ? on : off), LV_PART_MAIN);
}

static void wifiTick(lv_timer_t *) {
  wifiStep = (wifiStep + 1) % 4;
  wifiPaint();
}

static void createWifi(lv_obj_t *scr) {
  wifiBox = lv_obj_create(scr);
  lv_obj_remove_style_all(wifiBox);
  lv_obj_set_size(wifiBox, 84, 84);
  lv_obj_align(wifiBox, LV_ALIGN_CENTER, 0, 18);  // fan grows upward from its centre; shift down to look centred
  wifiDot = lv_obj_create(wifiBox);
  lv_obj_remove_style_all(wifiDot);
  lv_obj_set_size(wifiDot, 10, 10);
  lv_obj_set_style_radius(wifiDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(wifiDot, LV_OPA_COVER, 0);
  lv_obj_center(wifiDot);
  static const int diam[3] = {38, 62, 86};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *a = lv_arc_create(wifiBox);
    wifiArc[i] = a;
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, diam[i], diam[i]);
    lv_arc_set_rotation(a, 0);
    lv_arc_set_bg_angles(a, 225, 315);  // 90 degree fan pointing up
    lv_obj_set_style_arc_width(a, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(a, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_center(a);
  }
  lv_obj_add_flag(wifiBox, LV_OBJ_FLAG_HIDDEN);
}

// ---- "Synchronizing" animation: clock face whose hands spin (minute hand 12x the hour hand) ----
static lv_obj_t *syncBox, *syncFace, *syncMin, *syncHour, *syncDot, *syncLabel;
static lv_timer_t *syncTimer;
static float syncAngle;  // minute-hand angle in degrees, clockwise from 12
static lv_point_precise_t minPts[2], hourPts[2];  // must outlive the line objects
static constexpr int SYNC_C = 42;  // centre of the 84x84 box

static void syncPaint() {
  uint32_t on = night ? 0xC01515 : 0xFFFFFF, dim = night ? 0x801010 : 0xFF4D4D;
  lv_obj_set_style_border_color(syncFace, lv_color_hex(on), 0);
  lv_obj_set_style_line_color(syncMin, lv_color_hex(on), 0);
  lv_obj_set_style_line_color(syncHour, lv_color_hex(dim), 0);
  lv_obj_set_style_bg_color(syncDot, lv_color_hex(on), 0);
  lv_obj_set_style_text_color(syncLabel, lv_color_hex(on), 0);
}

static void syncHands() {
  float m = syncAngle * (float)M_PI / 180.0f, h = (syncAngle / 12.0f) * (float)M_PI / 180.0f;
  minPts[0] = {SYNC_C, SYNC_C};
  minPts[1] = {(lv_value_precise_t)lroundf(SYNC_C + 30 * sinf(m)), (lv_value_precise_t)lroundf(SYNC_C - 30 * cosf(m))};
  hourPts[0] = {SYNC_C, SYNC_C};
  hourPts[1] = {(lv_value_precise_t)lroundf(SYNC_C + 20 * sinf(h)), (lv_value_precise_t)lroundf(SYNC_C - 20 * cosf(h))};
  lv_line_set_points(syncMin, minPts, 2);
  lv_line_set_points(syncHour, hourPts, 2);
}

static void syncTick(lv_timer_t *) {
  syncAngle = fmodf(syncAngle + 9.0f, 360.0f);
  syncHands();
}

static lv_obj_t *makeHand(lv_obj_t *parent, int w) {
  lv_obj_t *l = lv_line_create(parent);
  lv_obj_set_pos(l, 0, 0);
  lv_obj_set_style_line_width(l, w, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return l;
}

static void createSync(lv_obj_t *scr) {
  syncBox = lv_obj_create(scr);
  lv_obj_remove_style_all(syncBox);
  lv_obj_set_size(syncBox, 84, 84);
  lv_obj_align(syncBox, LV_ALIGN_CENTER, 0, -14);
  syncFace = lv_obj_create(syncBox);
  lv_obj_remove_style_all(syncFace);
  lv_obj_set_size(syncFace, 84, 84);
  lv_obj_set_style_radius(syncFace, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(syncFace, 5, 0);
  lv_obj_set_style_border_opa(syncFace, LV_OPA_COVER, 0);
  syncHour = makeHand(syncBox, 5);
  syncMin = makeHand(syncBox, 4);
  syncDot = lv_obj_create(syncBox);
  lv_obj_remove_style_all(syncDot);
  lv_obj_set_size(syncDot, 10, 10);
  lv_obj_set_style_radius(syncDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(syncDot, LV_OPA_COVER, 0);
  lv_obj_center(syncDot);
  lv_obj_add_flag(syncBox, LV_OBJ_FLAG_HIDDEN);

  syncLabel = lv_label_create(scr);
  lv_obj_set_style_text_font(syncLabel, &manrope_20, 0);
  lv_label_set_text(syncLabel, "Synchronizing");
  lv_obj_align(syncLabel, LV_ALIGN_CENTER, 0, 52);
  lv_obj_add_flag(syncLabel, LV_OBJ_FLAG_HIDDEN);
  syncAngle = 0;
  syncHands();
  syncPaint();
}

// ---- "Scanning" animation: sonar pings expanding from the centre + a rotating sweep ----------
static constexpr int SONAR_RINGS = 3, SONAR_TRAIL = 3;
static constexpr float SONAR_R = 96.0f;  // stays inside the seconds-ring ticks
static lv_obj_t *sonarBox, *sonarRing[SONAR_RINGS], *sonarSweep[SONAR_TRAIL], *sonarDot, *sonarLabel;
static lv_timer_t *sonarTimer;
static float sonarPhase, sonarAngle;
static lv_point_precise_t sweepPts[SONAR_TRAIL][2];  // must outlive the line objects
static constexpr int SONAR_C = 100;  // centre of the 200x200 box

static uint32_t sonarColor() { return night ? 0xC01515 : 0x2EE59D; }

static void sonarPaint() {
  lv_color_t c = lv_color_hex(sonarColor());
  for (auto r : sonarRing) lv_obj_set_style_border_color(r, c, 0);
  for (auto l : sonarSweep) lv_obj_set_style_line_color(l, c, 0);
  lv_obj_set_style_bg_color(sonarDot, c, 0);
  lv_obj_set_style_text_color(sonarLabel, c, 0);
}

static void sonarFrame() {
  for (int i = 0; i < SONAR_RINGS; i++) {
    float ph = fmodf(sonarPhase + (float)i / SONAR_RINGS, 1.0f);
    int d = (int)(ph * SONAR_R * 2) + 4;
    lv_obj_set_size(sonarRing[i], d, d);
    lv_obj_center(sonarRing[i]);
    lv_obj_set_style_border_opa(sonarRing[i], (lv_opa_t)((1.0f - ph) * 200), 0);
  }
  for (int k = 0; k < SONAR_TRAIL; k++) {  // head plus two dimmer trailing lines
    float a = (sonarAngle - 14.0f * k) * (float)M_PI / 180.0f;
    sweepPts[k][0] = {SONAR_C, SONAR_C};
    sweepPts[k][1] = {(lv_value_precise_t)lroundf(SONAR_C + SONAR_R * sinf(a)), (lv_value_precise_t)lroundf(SONAR_C - SONAR_R * cosf(a))};
    lv_line_set_points(sonarSweep[k], sweepPts[k], 2);
  }
}

static void sonarTick(lv_timer_t *) {
  sonarPhase = fmodf(sonarPhase + 0.015f, 1.0f);
  sonarAngle = fmodf(sonarAngle + 6.0f, 360.0f);
  sonarFrame();
}

static void createSonar(lv_obj_t *scr) {
  sonarBox = lv_obj_create(scr);
  lv_obj_remove_style_all(sonarBox);
  lv_obj_set_size(sonarBox, 200, 200);
  lv_obj_center(sonarBox);
  for (auto &r : sonarRing) {
    r = lv_obj_create(sonarBox);
    lv_obj_remove_style_all(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, 2, 0);
    lv_obj_set_style_border_opa(r, LV_OPA_TRANSP, 0);
  }
  static const lv_opa_t trailOpa[SONAR_TRAIL] = {LV_OPA_COVER, LV_OPA_50, LV_OPA_20};
  for (int k = 0; k < SONAR_TRAIL; k++) {
    sonarSweep[k] = lv_line_create(sonarBox);
    lv_obj_set_pos(sonarSweep[k], 0, 0);
    lv_obj_set_style_line_width(sonarSweep[k], k == 0 ? 3 : 2, 0);
    lv_obj_set_style_line_opa(sonarSweep[k], trailOpa[k], 0);
    lv_obj_set_style_line_rounded(sonarSweep[k], true, 0);
  }
  sonarDot = lv_obj_create(sonarBox);
  lv_obj_remove_style_all(sonarDot);
  lv_obj_set_size(sonarDot, 8, 8);
  lv_obj_set_style_radius(sonarDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(sonarDot, LV_OPA_COVER, 0);
  lv_obj_center(sonarDot);
  lv_obj_add_flag(sonarBox, LV_OBJ_FLAG_HIDDEN);

  sonarLabel = lv_label_create(scr);
  lv_obj_set_style_text_font(sonarLabel, &manrope_20, 0);
  lv_label_set_text(sonarLabel, "Scanning");
  lv_obj_align(sonarLabel, LV_ALIGN_CENTER, 0, 70);
  lv_obj_add_flag(sonarLabel, LV_OBJ_FLAG_HIDDEN);
  sonarPaint();
}
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
  // Skeleton placeholder (icon circle, temperature bar, stacked hi/lo bars) shown while the first
  // fetch is in flight; roughly the size of the real content so nothing shifts when it arrives.
  skeleton = lv_obj_create(weatherRow);
  lv_obj_remove_style_all(skeleton);
  lv_obj_set_size(skeleton, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(skeleton, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(skeleton, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(skeleton, 8, 0);
  auto shape = [&](lv_obj_t *parent, int w, int h, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
  };
  skelShape[0] = shape(skeleton, 24, 24, LV_RADIUS_CIRCLE);
  skelShape[1] = shape(skeleton, 56, 16, 8);
  lv_obj_t *skelHilo = lv_obj_create(skeleton);
  lv_obj_remove_style_all(skelHilo);
  lv_obj_set_size(skelHilo, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(skelHilo, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(skelHilo, 4, 0);
  lv_obj_set_style_pad_left(skelHilo, 4, 0);
  skelShape[2] = shape(skelHilo, 24, 8, 4);
  skelShape[3] = shape(skelHilo, 24, 8, 4);
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
  weatherIcon = lv_label_create(weatherRow);
  lv_obj_set_style_text_font(weatherIcon, &weather_icons, 0);
  lv_label_set_text(weatherIcon, "");
  weatherLabel = lv_label_create(weatherRow);
  lv_obj_set_style_text_font(weatherLabel, &manrope_20, 0);
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
  lv_obj_set_style_text_font(hiLabel, &manrope_14, 0);
  lv_obj_set_style_text_color(hiLabel, lv_color_hex(0xFF8A65), 0);
  loLabel = lv_label_create(hiloBox);
  lv_obj_set_style_text_font(loLabel, &manrope_14, 0);
  lv_obj_set_style_text_color(loLabel, lv_color_hex(0x6FB6FF), 0);
  lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
  timeLabel = makeLabel(scr, &manrope_48, lv_color_white(), 0);  // dead centre
  // Date row: weekday in red, then "Nov 6" in gray, centred together.
  lv_obj_t *dateRow = lv_obj_create(scr);
  lv_obj_remove_style_all(dateRow);
  lv_obj_set_size(dateRow, 190, LV_SIZE_CONTENT);
  lv_obj_align(dateRow, LV_ALIGN_CENTER, 0, -58);
  lv_obj_set_flex_flow(dateRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dateRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dateRow, 6, 0);
  dayLabel = lv_label_create(dateRow);
  lv_obj_set_style_text_font(dayLabel, &manrope_20, 0);
  lv_obj_set_style_text_color(dayLabel, lv_color_hex(0xFF4D4D), 0);
  lv_label_set_text(dayLabel, "");
  dateLabel = lv_label_create(dateRow);
  lv_obj_set_style_text_font(dateLabel, &manrope_20, 0);
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xA0A0A0), 0);
  lv_label_set_text(dateLabel, "");

  status = lv_label_create(scr);
  lv_obj_set_style_text_font(status, &manrope_20, 0);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_center(status);
  lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
  createWifi(scr);
  createSync(scr);
  createSonar(scr);
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
  wifiPaint();
  syncPaint();
  sonarPaint();
  for (auto o : skelShape) lv_obj_set_style_bg_color(o, lv_color_hex(n ? 0x4A0E0E : 0x4A4F57), 0);
  for (int i = 0; i < TICK_COUNT; i++) styleTick(i, ringSec);
}

void ui_set_weather_loading(bool loading) {
  if (loading) {
    lv_obj_add_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(weatherLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(skeleton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(skeleton, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, const char *hi, const char *lo) {
  lv_label_set_text(weatherLabel, text ? text : "");
  lv_obj_remove_flag(weatherLabel, LV_OBJ_FLAG_HIDDEN);  // may have been hidden by the skeleton
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

void ui_set_connecting(bool on) {
  if (on) {
    ui_set_status(nullptr);
    wifiStep = 0;
    wifiPaint();
    lv_obj_remove_flag(wifiBox, LV_OBJ_FLAG_HIDDEN);
    if (!wifiTimer) wifiTimer = lv_timer_create(wifiTick, 350, nullptr);
  } else {
    if (wifiTimer) { lv_timer_delete(wifiTimer); wifiTimer = nullptr; }
    lv_obj_add_flag(wifiBox, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_set_syncing(bool on) {
  if (on) {
    ui_set_status(nullptr);
    syncAngle = 0;
    syncHands();
    syncPaint();
    lv_obj_remove_flag(syncBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(syncLabel, LV_OBJ_FLAG_HIDDEN);
    if (!syncTimer) syncTimer = lv_timer_create(syncTick, 50, nullptr);
  } else {
    if (syncTimer) { lv_timer_delete(syncTimer); syncTimer = nullptr; }
    lv_obj_add_flag(syncBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(syncLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_set_scanning(bool on) {
  if (on) {
    ui_set_status(nullptr);
    ui_set_connecting(false);
    ui_set_syncing(false);
    // Long-press from the running clock: blank the face (ticks, time, weather...) so only the sonar shows.
    // Never restored: setup mode ends in a reboot or switches to its own screen.
    lv_obj_t *scr = lv_obj_get_parent(sonarBox);
    for (uint32_t i = 0; i < lv_obj_get_child_count(scr); i++) {
      lv_obj_t *c = lv_obj_get_child(scr, i);
      if (c != sonarBox && c != sonarLabel) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    }
    sonarPhase = 0;
    sonarAngle = 0;
    sonarFrame();
    sonarPaint();
    lv_obj_remove_flag(sonarBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sonarLabel, LV_OBJ_FLAG_HIDDEN);
    if (!sonarTimer) sonarTimer = lv_timer_create(sonarTick, 40, nullptr);
  } else {
    if (sonarTimer) { lv_timer_delete(sonarTimer); sonarTimer = nullptr; }
    lv_obj_add_flag(sonarBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sonarLabel, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_set_style_text_font(l, &manrope_14, 0);
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
