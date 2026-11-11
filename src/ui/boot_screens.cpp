#include "ui_internal.h"

namespace ui {

static lv_obj_t *status;  // plain text message in the centre (boot / errors)

// ---- "Connecting" animation: WiFi glyph whose arcs light up in sequence ----------------
static lv_obj_t *wifiBox, *wifiDot, *wifiArc[3];
static lv_timer_t *wifiTimer;
static int wifiStep;

static void wifiPaint() {
  uint32_t on = night ? nightC(92) : 0xFFFFFF, off = night ? nightC(20) : 0x303030;
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
  uint32_t on = night ? nightC(92) : 0xFFFFFF, dim = night ? nightC(61) : THEMES[opt.theme].accent;
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

static uint32_t sonarColor() { return night ? nightC(92) : 0x2EE59D; }

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

void bootInit(lv_obj_t *scr) {
  status = lv_label_create(scr);
  lv_obj_set_style_text_font(status, &manrope_20, 0);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_center(status);
  lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
  createWifi(scr);
  createSync(scr);
  createSonar(scr);
}

// These only show during boot / setup and follow the target mode.
void bootPaint() {
  lv_obj_set_style_text_color(status, mixDN(0xFFFFFF, nightC(92)), 0);
  wifiPaint();
  syncPaint();
  sonarPaint();
}

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

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
