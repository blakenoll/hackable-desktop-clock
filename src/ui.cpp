#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <initializer_list>
#include <string.h>
#include "ui.h"

LV_FONT_DECLARE(weather_icons);
LV_FONT_DECLARE(manrope_14);
LV_FONT_DECLARE(manrope_20);
LV_FONT_DECLARE(manrope_56);

static lv_obj_t *skeleton, *skelShape[5], *skelHilo, *weatherRow, *weatherIcon, *weatherLabel, *hiloBox, *hiLabel, *loLabel, *rangeTrack, *rangeDot, *timeH, *timeC, *timeM, *ampmLabel, *dayLabel, *dateLabel, *status;


// ---- Seconds ring: 60 ticks around the rim --------------------------------------
// Elapsed seconds are softly lit, the current second is bright red, upcoming ones are dim.
static constexpr int TICK_COUNT = 60;
static constexpr float RING_CENTER = 119.5f;   // display is 240x240
static constexpr float TICK_OUTER = 117.0f;    // stay just inside the round bezel
static constexpr float TICK_MINOR_LEN = 7.0f;
static constexpr float TICK_MAJOR_LEN = 12.0f; // every 5th second
static lv_obj_t *ticks[TICK_COUNT];
static bool night = false;       // target mode (what ui_set_night last asked for)
static uint8_t nightMix = 0;     // current blend: 0 = full day colours, 255 = full night (animated)
static uint8_t accentIdx = 0;
static bool h24 = false;
static uint8_t dateFmt = 0;
static int lastMin = -1, lastDay = -1;  // ui_update caches; reset when options change

// Accent colours: weekday label + current-second tick. Night mode stays red whatever the accent.
struct Accent { uint32_t text, tick; };
static const Accent ACCENTS[] = {
    {0xFF4D4D, 0xFF3030},  // red
    {0xFFB020, 0xFFA010},  // amber
    {0x4CE08A, 0x30E070},  // green
    {0x36D6F0, 0x20D0F0},  // cyan
    {0xFF6EC7, 0xFF4DB8},  // pink
    {0xFFFFFF, 0xFFFFFF},  // white
};
static constexpr int ACCENT_COUNT = sizeof(ACCENTS) / sizeof(ACCENTS[0]);

// Vertical temperature range bar next to the temperature: high number at the top, low at the bottom.
static constexpr int RANGE_H = 34, RANGE_W = 4, RANGE_DOT = 10;

// Day/night pair blended by nightMix, so a day->night switch can fade instead of snapping.
static lv_color_t mixDN(uint32_t day, uint32_t nt) {
  return lv_color_mix(lv_color_hex(nt), lv_color_hex(day), nightMix);
}

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
  lv_obj_set_style_text_color(weatherIcon, mixDN(iconDayColor, 0xC01515), 0);
}
static int ringSec = -1;  // last second drawn on the ring (-1 = not drawn yet)
static lv_point_precise_t tickPts[TICK_COUNT][2];  // must outlive the line objects

// Elapsed ticks fade from a bright, "comet tail" just behind the current second down to a
// dim base level. The base is deliberately dim so the tail stands out.
static constexpr int TAIL_LEN = 12;

static void styleTick(int i, int sec) {
  bool major = i % 5 == 0;
  lv_color_t base = mixDN(0x56606D, 0x000000), bright = mixDN(0x8E9EB3, 0x000000), dim = mixDN(0x2A3138, 0x000000);
  // Tail colour for a tick `behind` seconds behind the head: bright just behind it, fading to base.
  auto tail = [&](int behind) {
    int f = 255 * (TAIL_LEN - behind + 1) / (TAIL_LEN + 1);  // 255 near the head -> ~0 at the tail end
    return lv_color_mix(bright, base, (lv_opa_t)(f * f / 255));  // squared: bright head, quick falloff
  };
  lv_color_t color;
  int width = major ? 3 : 2;
  if (i == sec) {
    color = mixDN(ACCENTS[accentIdx].tick, 0x000000);
    width = 3;
  } else if (i < sec) {
    int behind = sec - i;  // 1 = tick right before the current one
    color = behind <= TAIL_LEN ? tail(behind) : base;
  } else if (sec < 0) {
    color = dim;  // idle ring (no time yet)
  } else {
    // Ahead of the head. The tail wraps past 12 o'clock so it carries on from the previous minute, and
    // the ticks lit last minute dissolve to dim over the first TAIL_LEN seconds instead of snapping off.
    int wrapped = sec + TICK_COUNT - i;
    if (wrapped <= TAIL_LEN) color = tail(wrapped);
    else if (sec < TAIL_LEN) color = lv_color_mix(base, dim, (lv_opa_t)(255 * (TAIL_LEN - sec) / TAIL_LEN));
    else color = dim;
  }
  lv_obj_set_style_line_color(ticks[i], color, 0);
  lv_obj_set_style_line_width(ticks[i], width, 0);
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

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  createTicks(scr);

  // Weather row: [icon] 72F, centred below the time (kept inside the round bezel).
  weatherRow = lv_obj_create(scr);
  lv_obj_remove_style_all(weatherRow);
  lv_obj_set_size(weatherRow, 190, 36);
  lv_obj_align(weatherRow, LV_ALIGN_CENTER, 0, 62);
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
  // Mirrors the range bar: a thin vertical bar with a number bar at its top and bottom.
  skelHilo = lv_obj_create(skeleton);
  lv_obj_remove_style_all(skelHilo);
  lv_obj_set_size(skelHilo, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(skelHilo, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(skelHilo, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(skelHilo, 5, 0);
  lv_obj_set_style_pad_left(skelHilo, 4, 0);
  skelShape[2] = shape(skelHilo, RANGE_W, RANGE_H, RANGE_W / 2);
  lv_obj_t *skelNums = lv_obj_create(skelHilo);
  lv_obj_remove_style_all(skelNums);
  lv_obj_set_size(skelNums, 20, RANGE_H);
  skelShape[3] = shape(skelNums, 20, 8, 4);
  lv_obj_align(skelShape[3], LV_ALIGN_TOP_LEFT, 0, -1);
  skelShape[4] = shape(skelNums, 20, 8, 4);
  lv_obj_align(skelShape[4], LV_ALIGN_BOTTOM_LEFT, 0, 1);
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

  // Vertical range bar right of the temperature: [bar with dot] over [high / low numbers]. The dot marks
  // the current temperature between today's low (bottom) and high (top).
  hiloBox = lv_obj_create(weatherRow);
  lv_obj_remove_style_all(hiloBox);
  lv_obj_set_size(hiloBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hiloBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hiloBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hiloBox, 5, 0);
  lv_obj_set_style_pad_left(hiloBox, 4, 0);
  rangeTrack = lv_obj_create(hiloBox);
  lv_obj_remove_style_all(rangeTrack);
  lv_obj_set_size(rangeTrack, RANGE_W, RANGE_H);
  lv_obj_set_style_radius(rangeTrack, 2, 0);
  lv_obj_set_style_bg_opa(rangeTrack, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(rangeTrack, LV_GRAD_DIR_VER, 0);
  lv_obj_add_flag(rangeTrack, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // the dot is wider than the track
  rangeDot = lv_obj_create(rangeTrack);
  lv_obj_remove_style_all(rangeDot);
  lv_obj_set_size(rangeDot, RANGE_DOT, RANGE_DOT);
  lv_obj_set_style_radius(rangeDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(rangeDot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rangeDot, 2, 0);  // black outline separates the dot from the gradient bar
  lv_obj_set_style_border_color(rangeDot, lv_color_black(), 0);
  lv_obj_set_style_border_opa(rangeDot, LV_OPA_COVER, 0);
  lv_obj_t *nums = lv_obj_create(hiloBox);  // high pinned to the top, low to the bottom of the bar's height
  lv_obj_remove_style_all(nums);
  lv_obj_set_size(nums, 24, RANGE_H);
  hiLabel = lv_label_create(nums);
  lv_obj_set_style_text_font(hiLabel, &manrope_14, 0);
  lv_obj_set_style_text_color(hiLabel, lv_color_hex(0xFF8A65), 0);
  lv_obj_align(hiLabel, LV_ALIGN_TOP_LEFT, 0, -3);
  loLabel = lv_label_create(nums);
  lv_obj_set_style_text_font(loLabel, &manrope_14, 0);
  lv_obj_set_style_text_color(loLabel, lv_color_hex(0x6FB6FF), 0);
  lv_obj_align(loLabel, LV_ALIGN_BOTTOM_LEFT, 0, 3);
  lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);

  // Time row, dead centre: hours, blinking colon, minutes. The small AM/PM is NOT in the row (it would
  // pull the digits off-centre); ui_update() hangs it off the right of the minutes, on the baseline.
  lv_obj_t *timeRow = lv_obj_create(scr);
  lv_obj_remove_style_all(timeRow);
  lv_obj_set_size(timeRow, 200, LV_SIZE_CONTENT);
  lv_obj_align(timeRow, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flex_flow(timeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(timeRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(timeRow, 0, 0);
  auto digits = [&](const char *t) {
    lv_obj_t *l = lv_label_create(timeRow);
    lv_obj_set_style_text_font(l, &manrope_56, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, t);
    return l;
  };
  timeH = digits("");
  timeC = digits(":");
  timeM = digits("");
  lv_obj_add_flag(timeC, LV_OBJ_FLAG_HIDDEN);  // shown with the first real time, so no lone ":" during boot screens
  ampmLabel = lv_label_create(scr);
  lv_obj_set_style_text_font(ampmLabel, &manrope_14, 0);
  lv_obj_set_style_text_color(ampmLabel, lv_color_hex(0xA0A0A0), 0);
  lv_label_set_text(ampmLabel, "");
  // Date row: weekday in red, then "Nov 6" in gray, centred together.
  lv_obj_t *dateRow = lv_obj_create(scr);
  lv_obj_remove_style_all(dateRow);
  lv_obj_set_size(dateRow, 190, LV_SIZE_CONTENT);
  lv_obj_align(dateRow, LV_ALIGN_CENTER, 0, -62);
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
// ---- Range dot ---------------------------------------------------------------------------
static int32_t rangeFrac = 500;  // 0..1000 = current temp's position between today's low (0) and high (1000)

static void rangePlace() {
  float f = rangeFrac / 1000.0f;
  lv_obj_set_pos(rangeDot, -(RANGE_DOT - RANGE_W) / 2, (int)lroundf((1.0f - f) * (RANGE_H - RANGE_DOT)));
}

static void rangeExec(void *, int32_t v) {
  rangeFrac = v;
  rangePlace();
}

// ---- Minute-change transition: the changed digits fade out (rising), swap text, fade back in ----
struct Swap { lv_obj_t *label; char text[32]; };
enum { SW_HOURS, SW_MINUTES, SW_ICON, SW_TEMP, SW_HI, SW_LO, SW_COUNT };
static Swap swaps[SW_COUNT];
static uint32_t pendingIconColor;  // applied when the icon glyph actually swaps

static void placeAmpm() {
  lv_obj_update_layout(lv_obj_get_parent(timeM));  // digits changed width: re-place AM/PM beside them
  lv_obj_align_to(ampmLabel, timeM, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, 4);
}

// Weather fades rise a few pixels; the time digits crossfade in place (movement made the whole string bounce).
static int riseFor(void *o) { return (o == timeH || o == timeM) ? 0 : 6; }

static void fadeOutExec(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
  lv_obj_set_style_translate_y((lv_obj_t *)o, -(255 - v) * riseFor(o) / 255, 0);
}

static void fadeInExec(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
  lv_obj_set_style_translate_y((lv_obj_t *)o, (255 - v) * riseFor(o) / 255, 0);
}

// Fade an object in (rising slightly). Starts fully transparent so there is no one-frame flash.
static void fadeIn(lv_obj_t *o) {
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
  lv_anim_t in;
  lv_anim_init(&in);
  lv_anim_set_var(&in, o);
  lv_anim_set_exec_cb(&in, fadeInExec);
  lv_anim_set_values(&in, 0, 255);
  lv_anim_set_duration(&in, 260);
  lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
  lv_anim_start(&in);
}

static void swapOutDone(lv_anim_t *a) {
  Swap *sw = (Swap *)lv_anim_get_user_data(a);
  lv_label_set_text(sw->label, sw->text);
  if (sw->label == weatherIcon) {  // new glyph arrives with its own colour
    iconDayColor = pendingIconColor;
    applyIconColor();
  }
  placeAmpm();
  fadeIn(sw->label);
}

// Text change with a quick fade-out / fade-in. A hidden label just appears (fade-in only).
static void swapText(int idx, lv_obj_t *label, const char *text) {
  if (lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
    lv_anim_delete(label, nullptr);
    lv_label_set_text(label, text);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    if (label == weatherIcon) { iconDayColor = pendingIconColor; applyIconColor(); }
    fadeIn(label);
    return;
  }
  if (!strcmp(lv_label_get_text(label), text)) return;
  swaps[idx].label = label;
  strncpy(swaps[idx].text, text, sizeof(swaps[idx].text) - 1);
  swaps[idx].text[sizeof(swaps[idx].text) - 1] = 0;
  lv_anim_delete(label, nullptr);
  lv_anim_t out;
  lv_anim_init(&out);
  lv_anim_set_var(&out, label);
  lv_anim_set_exec_cb(&out, fadeOutExec);
  lv_anim_set_values(&out, 255, 0);
  lv_anim_set_duration(&out, 160);
  lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
  lv_anim_set_user_data(&out, &swaps[idx]);
  lv_anim_set_completed_cb(&out, swapOutDone);
  lv_anim_start(&out);
}

void ui_update(const tm &t) {
  char buf[24];

  if (t.tm_sec != ringSec) {  // seconds ring (not drawn at all once night is fully faded in)
    if (nightMix == 255) {
      // hidden: nothing to restyle
    } else if (ringSec < 0 || t.tm_sec <= TAIL_LEN || t.tm_sec < ringSec) {  // first draw, new minute, or last minute's ticks still dissolving: restyle everything
      for (int i = 0; i < TICK_COUNT; i++) styleTick(i, t.tm_sec);
    } else {
      for (int i = ringSec > TAIL_LEN ? ringSec - TAIL_LEN : 0; i <= t.tm_sec; i++) styleTick(i, t.tm_sec);  // tail + skipped seconds
    }
    ringSec = t.tm_sec;
  }

  if (t.tm_min != lastMin) {  // redraw the big text only when the minute changes
    bool first = lastMin < 0;
    lastMin = t.tm_min;
    char hh[8], mm[8];
    if (h24) snprintf(hh, sizeof(hh), "%02d", t.tm_hour);
    else snprintf(hh, sizeof(hh), "%d", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12);
    snprintf(mm, sizeof(mm), "%02d", t.tm_min);
    lv_label_set_text(ampmLabel, h24 ? "" : (t.tm_hour < 12 ? "AM" : "PM"));
    if (first) {  // no transition on the very first draw
      lv_obj_remove_flag(timeC, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(timeH, hh);
      lv_label_set_text(timeM, mm);
      placeAmpm();
    } else {
      swapText(SW_HOURS, timeH, hh);
      swapText(SW_MINUTES, timeM, mm);
    }
  }

  if (t.tm_yday != lastDay) {
    lastDay = t.tm_yday;
    char wday[8], mon[8];
    strftime(wday, sizeof(wday), "%a", &t);
    strftime(mon, sizeof(mon), "%b", &t);
    lv_label_set_text(dayLabel, wday);
    if (dateFmt == 1) snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, mon);
    else snprintf(buf, sizeof(buf), "%s %d", mon, t.tm_mday);  // %d would zero-pad single digits
    lv_label_set_text(dateLabel, buf);
  }
}

static bool rangeWanted = false;  // weather supplied a range
// At night the range bar is invisible but keeps its layout slot, and the row slides sideways in step
// with the fade so the icon + temperature end up centred (no jump when the bar disappears).
static void rangeNightSync() {
  static int slot = 0;
  if (rangeWanted && !lv_obj_has_flag(hiloBox, LV_OBJ_FLAG_HIDDEN) && slot == 0) {
    lv_obj_update_layout(weatherRow);
    slot = lv_obj_get_width(hiloBox) + 5;  // + the row's column gap
  }
  lv_obj_set_style_translate_x(weatherRow, rangeWanted ? slot * nightMix / 510 : 0, 0);
}

// Recolours everything for the current nightMix (and accent).
static void applyTheme() {
  for (auto l : {timeH, timeC, timeM}) lv_obj_set_style_text_color(l, mixDN(0xFFFFFF, 0xD01818), 0);
  lv_obj_set_style_text_color(ampmLabel, mixDN(0xA0A0A0, 0x901010), 0);
  lv_obj_set_style_text_color(dayLabel, mixDN(ACCENTS[accentIdx].text, 0xD01818), 0);  // weekday: brighter
  lv_obj_set_style_text_color(dateLabel, mixDN(0xA0A0A0, 0x901010), 0);
  lv_obj_set_style_text_color(weatherLabel, mixDN(0xD0D8E0, 0xC01515), 0);
  // Quiet night face: the range bar and high/low fade to black (the layout slot is freed once fully faded).
  lv_obj_set_style_text_color(hiLabel, mixDN(0xFF8A65, 0x000000), 0);
  lv_obj_set_style_text_color(loLabel, mixDN(0x6FB6FF, 0x000000), 0);
  lv_obj_set_style_bg_color(rangeTrack, mixDN(0xFF8A65, 0x000000), 0);       // top = high
  lv_obj_set_style_bg_grad_color(rangeTrack, mixDN(0x6FB6FF, 0x000000), 0);  // bottom = low
  lv_obj_set_style_bg_color(rangeDot, mixDN(0xFFFFFF, 0x000000), 0);
  lv_obj_set_style_border_color(rangeDot, lv_color_black(), 0);
  rangeNightSync();
  lv_obj_set_style_text_color(status, mixDN(0xFFFFFF, 0xC01515), 0);
  applyIconColor();
  wifiPaint();    // these three only show during boot/setup and follow the target mode
  syncPaint();
  sonarPaint();
  // Night: the range-bar part of the skeleton fades to black like the real bar, and the row slides to stay centred.
  for (int k = 0; k < 5; k++) lv_obj_set_style_bg_color(skelShape[k], mixDN(0x4A4F57, k >= 2 ? 0x000000 : 0x4A0E0E), 0);
  constexpr int skelSlot = 4 + RANGE_W + 5 + 20 + 8;  // pad + bar + gap + number bars + the row's gap
  lv_obj_set_style_translate_x(skeleton, skelSlot * nightMix / 510, 0);
  // Quiet night: the ring fades to black along with everything else, then is hidden (no motion at all).
  for (int i = 0; i < TICK_COUNT; i++) {
    if (nightMix == 255) {
      lv_obj_add_flag(ticks[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(ticks[i], LV_OBJ_FLAG_HIDDEN);
      styleTick(i, ringSec);
    }
  }
}

static uint8_t appliedMix = 0;
static void nightExec(void *, int32_t v) {
  nightMix = (uint8_t)v;
  // Restyling 60 ticks + labels every frame is wasteful; steps of 4/255 are invisible.
  if (abs((int)v - (int)appliedMix) >= 4 || v == 0 || v == 255) {
    appliedMix = nightMix;
    applyTheme();
  }
}

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
  wifiPaint();  // boot/setup widgets follow the target immediately
  syncPaint();
  sonarPaint();
}

void ui_set_options(bool use24, int dateOrder, int accent) {
  h24 = use24;
  dateFmt = dateOrder == 1 ? 1 : 0;
  accentIdx = (accent >= 0 && accent < ACCENT_COUNT) ? accent : 0;
  lastMin = lastDay = -1;  // redraw time/date in the new format on the next update
  applyTheme();
}

// A comet (head + fading tail) runs once around the ring, leaving no trace.
void ui_boot_splash() {
  if (nightMix == 255) return;  // no ring at night
  for (int step = 0; step < TICK_COUNT + TAIL_LEN + 2; step++) {
    int lo = step - TAIL_LEN - 1 < 0 ? 0 : step - TAIL_LEN - 1;
    int hi = step < TICK_COUNT - 1 ? step : TICK_COUNT - 1;
    for (int i = lo; i <= hi; i++) styleTick(i, i > step - TAIL_LEN - 1 ? step : -1);
    lv_timer_handler();
    delay(14);
  }
  ringSec = -1;
}

void ui_set_weather_loading(bool loading) {
  if (loading) {
    for (lv_obj_t *o : {weatherIcon, weatherLabel, hiloBox}) {
      lv_anim_delete(o, nullptr);  // drop any fade in flight so the object is clean when it reappears
      lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(skeleton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(skeleton, LV_OBJ_FLAG_HIDDEN);
  }
}

// Updates fade (out, swap, in) instead of snapping; content appearing after the skeleton fades in.
// The range dot glides to its new position.
void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, bool hasRange, int lo, int hi, float cur) {
  swapText(SW_TEMP, weatherLabel, text ? text : "");
  rangeWanted = hasRange;
  if (hasRange) {
    char h[12], l[12];
    snprintf(h, sizeof(h), "%d", hi);
    snprintf(l, sizeof(l), "%d", lo);
    float f = hi > lo ? (cur - lo) / (float)(hi - lo) : 0.5f;
    int32_t target = (int32_t)lroundf(fminf(fmaxf(f, 0.0f), 1.0f) * 1000);
    lv_anim_delete(&rangeFrac, nullptr);
    if (lv_obj_has_flag(hiloBox, LV_OBJ_FLAG_HIDDEN)) {  // first appearance: place, then fade in
      lv_label_set_text(hiLabel, h);
      lv_label_set_text(loLabel, l);
      rangeFrac = target;
      rangePlace();
      lv_obj_remove_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
      fadeIn(hiloBox);
      rangeNightSync();
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
  } else {
    lv_anim_delete(hiloBox, nullptr);
    lv_obj_add_flag(hiloBox, LV_OBJ_FLAG_HIDDEN);
    rangeNightSync();
  }
  if (icon) {
    pendingIconColor = iconColor;
    if (!lv_obj_has_flag(weatherIcon, LV_OBJ_FLAG_HIDDEN) && !strcmp(lv_label_get_text(weatherIcon), icon)) {
      iconDayColor = iconColor;  // same glyph, maybe a new colour: no fade needed
      applyIconColor();
    } else {
      swapText(SW_ICON, weatherIcon, icon);
    }
  } else {
    lv_anim_delete(weatherIcon, nullptr);
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
