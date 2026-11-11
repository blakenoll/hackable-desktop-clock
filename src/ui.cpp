#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <initializer_list>
#include <string.h>
#include "ui.h"

LV_FONT_DECLARE(weather_icons);
LV_FONT_DECLARE(weather_icons_42);
LV_FONT_DECLARE(manrope_14);
LV_FONT_DECLARE(manrope_20);
LV_FONT_DECLARE(manrope_34);
LV_FONT_DECLARE(manrope_56);
LV_FONT_DECLARE(manrope_74);
LV_FONT_DECLARE(manrope_light_64);
LV_FONT_DECLARE(manrope_caps_12);  // bold small caps: 0x20-0x5F (no lowercase), degree sign, middle dot

// ---- Options, themes, night colour -------------------------------------------------------
static UiOptions opt;
static bool night = false;       // target mode (what ui_set_night last asked for)
static uint8_t nightMix = 0;     // current blend: 0 = full day colours, 255 = full night (animated)

// Theme presets (setup page; NVS key `accent`, so the first six keep the old accent order).
// digits: time; accent: weekday etc.; tick: current-second mark; sub: secondary text; colon: dim text;
// base / bright / dim: seconds-ring greys (elapsed, comet tail, upcoming).
struct Theme { uint32_t digits, accent, tick, sub, colon, base, bright, dim; };
static const Theme THEMES[] = {
    {0xFFFFFF, 0xFF4D4D, 0xFF3030, 0x9AA3AD, 0x6B7480, 0x56606D, 0x8E9EB3, 0x2A3138},  // Red
    {0xFFF1D6, 0xFFB020, 0xFFA010, 0xB0A088, 0x8A775C, 0x6B5E48, 0xB09A70, 0x332C1F},  // Amber
    {0xE8FFF1, 0x4CE08A, 0x30E070, 0x8FAA9A, 0x5F7A6B, 0x4C6557, 0x82AD94, 0x1F2E26},  // Mint
    {0xE3F4FF, 0x36D6F0, 0x20D0F0, 0x8AA4B5, 0x5E7A8C, 0x4A6272, 0x7FA6BC, 0x1F2D36},  // Ice
    {0xFFE8F5, 0xFF6EC7, 0xFF4DB8, 0xB091A4, 0x86677A, 0x695062, 0xB085A0, 0x30222B},  // Orchid
    {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x9AA3AD, 0x6B7480, 0x56606D, 0x8E9EB3, 0x2A3138},  // Mono
    {0xFFEBD6, 0xFF7A3D, 0xFF6A28, 0xB09A88, 0x8A6F5C, 0x6B5648, 0xB08A70, 0x33291F},  // Ember
};
static constexpr int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// Night colours. Everything shown at night is this colour at some brightness (nightC(percent)).
static const uint32_t NIGHTS[] = {0xD01818, 0xC8780A, 0x6E6E6E};  // red, amber, dim white
static constexpr int NIGHT_COUNT = sizeof(NIGHTS) / sizeof(NIGHTS[0]);

static uint32_t scaleHex(uint32_t c, int pct) {
  uint32_t r = ((c >> 16) & 0xFF) * pct / 100, g = ((c >> 8) & 0xFF) * pct / 100, b = (c & 0xFF) * pct / 100;
  return (r << 16) | (g << 8) | b;
}
static uint32_t mixHex(uint32_t a, uint32_t b, int pctA) {  // pctA = 100 -> a
  auto ch = [&](int sh) { return (((a >> sh) & 0xFF) * pctA + ((b >> sh) & 0xFF) * (100 - pctA)) / 100; };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
static uint32_t nightC(int pct) { return scaleHex(NIGHTS[opt.nightColor], pct); }

// Day/night pair blended by nightMix, so a day->night switch can fade instead of snapping.
static lv_color_t mixDN(uint32_t day, uint32_t nt) {
  return lv_color_mix(lv_color_hex(nt), lv_color_hex(day), nightMix);
}

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
// ==== Clock faces ===============================================================================
// Everything on the clock face lives under faceRoot and is torn down and rebuilt when the face (or an
// option) changes: ringBox holds the ring, content holds the rest and stays hidden until the first
// ui_update() so the boot animations have the centre to themselves. A face only creates the objects it
// uses, so every pointer below may be null: code that touches them goes through the null-safe helpers
// or checks first. What is shown (time, weather...) is cached and re-applied after a rebuild.
static constexpr float RING_CENTER = 119.5f;  // display is 240x240

static lv_obj_t *faceRoot, *ringBox, *content;
static lv_obj_t *timeH, *timeC, *timeM, *ampmLabel, *secLabel, *dayLabel, *dateLabel, *date2Label, *sunLabel;
static lv_obj_t *weatherRow, *weatherIcon, *weatherLabel, *skeleton, *skelShape[5];
static lv_obj_t *hiloBox, *hiLabel, *loLabel, *rangeTrack, *rangeDot;
static lv_obj_t *detailBox, *detailLabel, *offBox, *offIcon, *offSlash, *winBox;
static constexpr int DROP_COUNT = 4;
static lv_obj_t *drops[DROP_COUNT];
static bool floatAmpm;  // AM/PM (and the seconds digits) hang off the right of the minutes

// Cached model, so a rebuilt face can be filled in again.
static tm lastTm;
static bool haveTm = false;
static int lastMin = -1, lastHour = -1, lastDay = -1;  // ui_update caches; reset on rebuild
static struct {
  bool set = false, hasIcon = false, hasRange = false;
  char icon[8] = "", text[32] = "";
  uint32_t iconColor = 0xFFFFFF;
  int lo = 0, hi = 0, fall = 0;
  float cur = 0;
} wx;
static char detailText[32] = "";
static bool loadingShown = false, staleShown = false, offShown = false;
static constexpr int RAIN_BARS = 6;
static int rainPct[RAIN_BARS], tempHr[RAIN_BARS], rainStart = -1;  // hourly rain chance from the hour `rainStart` (-1 = none)
static int sunriseMin = -1, sunsetMin = -1;     // minutes after midnight (-1 = unknown)

// ---- Small helpers -----------------------------------------------------------------------
static lv_obj_t *box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}
static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, int spacing = 0) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  if (spacing) lv_obj_set_style_text_letter_space(l, spacing, 0);
  lv_label_set_text(l, "");
  return l;
}
static lv_obj_t *mkLabelAt(const lv_font_t *font, int spacing, int x, int y) {
  lv_obj_t *l = mkLabel(content, font, spacing);
  lv_obj_align(l, LV_ALIGN_CENTER, x, y);
  return l;
}
static lv_obj_t *mkRow(lv_obj_t *parent, int w, int x, int y, int gap, bool column = false) {
  lv_obj_t *o = box(parent);
  lv_obj_set_size(o, w, LV_SIZE_CONTENT);
  lv_obj_align(o, LV_ALIGN_CENTER, x, y);
  lv_obj_set_flex_flow(o, column ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(o, gap, 0);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}
static lv_obj_t *mkDisc(lv_obj_t *parent, int size) {
  lv_obj_t *o = box(parent);
  lv_obj_set_size(o, size, size);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}
static void tc(lv_obj_t *o, lv_color_t c) { if (o) lv_obj_set_style_text_color(o, c, 0); }
static void bgc(lv_obj_t *o, lv_color_t c) { if (o) lv_obj_set_style_bg_color(o, c, 0); }
static void setText(lv_obj_t *o, const char *t) { if (o) lv_label_set_text(o, t); }
static void setHidden(lv_obj_t *o, bool hide) {
  if (!o) return;
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
// Point at `r` pixels from the centre, `deg` clockwise from 12 o'clock.
static void polar(float r, float deg, float &x, float &y) {
  float a = deg * (float)M_PI / 180.0f;
  x = RING_CENTER + r * sinf(a);
  y = RING_CENTER - r * cosf(a);
}

// ---- Rings ---------------------------------------------------------------------------------
// K_TICKS: 60 line ticks with a comet tail. K_DOTTICKS: same, minute ticks drawn as dots.
// K_BLOCKS: chunky LCD-style blocks that fill up. K_MARKERS: static analog-dial batons.
// K_ARC: one arc that grows with the seconds. K_ORBIT: a single dot on a hairline circle.
// K_DAYLIGHT: 24 h dial, noon at the top: sunrise-to-sunset arc with a sun dot at "now".
enum RingKind { K_NONE, K_TICKS, K_DOTTICKS, K_BLOCKS, K_MARKERS, K_ARC, K_ORBIT, K_DAYLIGHT };
static RingKind rk = K_NONE;
static constexpr int TICK_COUNT = 60;
static lv_obj_t *ticks[TICK_COUNT];
static lv_point_precise_t tickPts[TICK_COUNT][2];  // must outlive the line objects
static int ringSec = -1;  // last second drawn on the ring (-1 = not drawn yet)
static lv_obj_t *ringArc, *orbitRing, *orbitDot, *dayTrack, *dayArc, *sunDot;
static bool sunUp = true;

static RingKind ringKind() {
  static const RingKind def[FACE_COUNT] = {K_DOTTICKS, K_ARC, K_TICKS, K_MARKERS, K_ORBIT, K_DOTTICKS, K_BLOCKS, K_DAYLIGHT};
  return def[opt.face];
}
static bool tickKind() { return rk == K_TICKS || rk == K_DOTTICKS || rk == K_BLOCKS; }  // has a comet (splash, chime)

// Tick geometry per ring kind (radii and lengths in pixels, tuned by eye on the real display).
struct TickGeo { float outer, minorOuter, minorLen, majorLen, quarterLen; uint8_t wMinor, wMajor, wHead; bool round; };
static const TickGeo &tickGeo() {
  static const TickGeo ticksG = {117, 117, 7, 12, 12, 2, 3, 3, true};
  static const TickGeo dots = {117, 114.5f, 2, 10, 10, 3, 3, 4, true};
  static const TickGeo blocks = {117, 117, 8, 12, 12, 4, 4, 4, false};
  static const TickGeo markers = {116, 116, 4, 12, 18, 2, 3, 3, false};
  return rk == K_DOTTICKS ? dots : rk == K_BLOCKS ? blocks : rk == K_MARKERS ? markers : ticksG;
}

// Elapsed ticks fade from a bright "comet tail" just behind the current second down to a dim base
// level. The base is deliberately dim so the tail stands out. The tail is tinted with the accent.
static constexpr int TAIL_LEN = 12;

static void styleTick(int i, int sec) {
  if (!ticks[i]) return;
  const Theme &T = THEMES[opt.theme];
  const TickGeo &g = tickGeo();
  bool major = i % 5 == 0;
  int width = major ? g.wMajor : g.wMinor;
  lv_color_t color;
  if (rk == K_MARKERS) {  // static dial; at night the batons stay (dim) so the hands can still be read
    if (i % 15 == 0) { color = mixDN(T.digits, nightC(55)); width = 4; }
    else if (major) color = mixDN(T.bright, nightC(35));
    else color = mixDN(T.dim, 0x000000);
  } else if (rk == K_BLOCKS) {
    if (i == sec) color = mixDN(T.tick, 0x000000);
    else if (sec >= 0 && i < sec) color = mixDN(scaleHex(T.tick, 50), 0x000000);
    else color = mixDN(scaleHex(T.tick, 13), 0x000000);
  } else {
    lv_color_t base = mixDN(T.base, 0x000000), bright = mixDN(mixHex(T.tick, T.bright, 55), 0x000000), dim = mixDN(T.dim, 0x000000);
    // Tail colour for a tick `behind` seconds behind the head: bright just behind it, fading to base.
    auto tail = [&](int behind) {
      int f = 255 * (TAIL_LEN - behind + 1) / (TAIL_LEN + 1);  // 255 near the head -> ~0 at the tail end
      return lv_color_mix(bright, base, (lv_opa_t)(f * f / 255));  // squared: bright head, quick falloff
    };
    if (i == sec) {
      color = mixDN(T.tick, 0x000000);
      width = g.wHead;
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
  }
  lv_obj_set_style_line_color(ticks[i], color, 0);
  lv_obj_set_style_line_width(ticks[i], width, 0);
}

static void createTicks() {
  const TickGeo &g = tickGeo();
  for (int i = 0; i < TICK_COUNT; i++) {
    float deg = i * 6.0f;  // clockwise from 12 o'clock
    bool major = i % 5 == 0;
    float rOut = major ? g.outer : g.minorOuter;
    float rIn = rOut - (i % 15 == 0 ? g.quarterLen : major ? g.majorLen : g.minorLen);
    float x1, y1, x2, y2;
    polar(rIn, deg, x1, y1);
    polar(rOut, deg, x2, y2);
    // lv_line points are relative to the object, which sits at the segment's bounding-box corner.
    float minx = fminf(x1, x2), miny = fminf(y1, y2);
    tickPts[i][0] = {(lv_value_precise_t)lroundf(x1 - minx), (lv_value_precise_t)lroundf(y1 - miny)};
    tickPts[i][1] = {(lv_value_precise_t)lroundf(x2 - minx), (lv_value_precise_t)lroundf(y2 - miny)};
    ticks[i] = lv_line_create(ringBox);
    lv_obj_remove_style_all(ticks[i]);
    lv_line_set_points(ticks[i], tickPts[i], 2);
    lv_obj_set_pos(ticks[i], (int32_t)lroundf(minx), (int32_t)lroundf(miny));
    lv_obj_set_style_line_rounded(ticks[i], g.round, 0);
    styleTick(i, -1);
  }
}

// Bare arc (no knob, not clickable) filling the ring; angle 0 is 12 o'clock.
static lv_obj_t *mkRingArc(int width) {
  lv_obj_t *a = lv_arc_create(ringBox);
  lv_obj_remove_style_all(a);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(a, 228, 228);
  lv_obj_center(a);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  for (uint32_t part : {(uint32_t)LV_PART_MAIN, (uint32_t)LV_PART_INDICATOR}) {
    lv_obj_set_style_arc_width(a, width, part);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, part);
    lv_obj_set_style_arc_rounded(a, true, part);
  }
  return a;
}

static void arcExec(void *o, int32_t v) { lv_arc_set_value((lv_obj_t *)o, v); }

// The arc glides to the new second; at the top of the minute it unwinds back to empty.
static void arcTo(int sec, bool animate) {
  int target = sec < 0 ? 0 : sec * 6, cur = lv_arc_get_value(ringArc);
  lv_anim_delete(ringArc, arcExec);
  if (!animate || cur == target) {
    lv_arc_set_value(ringArc, target);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ringArc);
  lv_anim_set_exec_cb(&a, arcExec);
  lv_anim_set_values(&a, cur, target);
  lv_anim_set_duration(&a, target < cur ? 500 : 300);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static constexpr float ORBIT_R = 110.0f;
static constexpr int ORBIT_DOT = 9;
static int32_t orbitA;  // tenths of a degree, clockwise from 12 o'clock

static void orbitExec(void *, int32_t v) {
  orbitA = v;
  if (!orbitDot) return;
  float x, y;
  polar(ORBIT_R, v / 10.0f, x, y);
  lv_obj_set_pos(orbitDot, (int32_t)lroundf(x - ORBIT_DOT / 2.0f), (int32_t)lroundf(y - ORBIT_DOT / 2.0f));
}

static void orbitTo(int sec, bool animate) {
  int32_t target = sec < 0 ? 0 : sec * 60, cur = ((orbitA % 3600) + 3600) % 3600;
  lv_anim_delete(&orbitA, nullptr);
  if (!animate || cur == target) {
    orbitExec(nullptr, target);
    return;
  }
  if (target < cur) cur -= 3600;  // keep going clockwise through 12 o'clock
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &orbitA);
  lv_anim_set_exec_cb(&a, orbitExec);
  lv_anim_set_values(&a, cur, target);
  lv_anim_set_duration(&a, 350);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void fmtClock(int minutes, char *out, size_t n) {  // "6:42 AM" or "06:42"
  int h = minutes / 60, m = minutes % 60;
  if (opt.h24) snprintf(out, n, "%02d:%02d", h, m);
  else snprintf(out, n, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
}

static void daylightPaint() {
  if (!dayTrack) return;
  lv_obj_set_style_arc_color(dayTrack, mixDN(0x18233D, 0x000000), LV_PART_MAIN);
  lv_obj_set_style_arc_color(dayArc, mixDN(0x5A4520, 0x000000), LV_PART_MAIN);       // daylight still to come
  lv_obj_set_style_arc_color(dayArc, mixDN(0xFFC940, 0x000000), LV_PART_INDICATOR);  // daylight so far
  bgc(sunDot, mixDN(sunUp ? 0xFFE27A : 0xC9D6FF, 0x000000));  // sun by day, pale moon after dark
}

// Daylight dial + the line under the time. Runs each minute and when sunrise/sunset arrive.
static void daylightUpdate() {
  if (!haveTm) return;
  int now = lastTm.tm_hour * 60 + lastTm.tm_min;
  bool have = sunriseMin >= 0 && sunsetMin > sunriseMin;
  sunUp = !have || (now >= sunriseMin && now < sunsetMin);
  if (rk == K_DAYLIGHT && dayTrack) {
    auto ang = [](int min) { return ((min - 720) / 4 + 360) % 360; };  // 4 minutes per degree, noon on top
    setHidden(dayArc, !have);
    if (have) {
      int upTo = sunUp ? now : sunriseMin;  // after dark the whole span is dim
      lv_arc_set_bg_angles(dayArc, ang(sunriseMin), ang(sunsetMin));
      lv_arc_set_angles(dayArc, ang(sunriseMin), ang(upTo));  // after set_bg_angles, which resets it
    }
    float x, y;
    polar(111, (now - 720) / 4.0f, x, y);
    lv_obj_set_pos(sunDot, (int32_t)lroundf(x - 6.5f), (int32_t)lroundf(y - 6.5f));
    setHidden(sunDot, false);
    daylightPaint();
  }
  if (sunLabel) {
    char buf[32] = "", clk[12];
    if (have && sunUp) {
      int left = sunsetMin - now;
      snprintf(buf, sizeof(buf), "DAYLIGHT %dH %02dM", left / 60, left % 60);
    } else if (have) {
      fmtClock(sunriseMin, clk, sizeof(clk));
      snprintf(buf, sizeof(buf), "SUNRISE %s", clk);
    }
    lv_label_set_text(sunLabel, buf);
  }
}

static void buildRing() {
  switch (rk) {
    case K_TICKS: case K_DOTTICKS: case K_BLOCKS: case K_MARKERS:
      createTicks();
      break;
    case K_ARC:
      ringArc = mkRingArc(5);
      lv_arc_set_range(ringArc, 0, 360);
      lv_arc_set_value(ringArc, 0);
      lv_obj_set_style_bg_opa(ringArc, LV_OPA_COVER, LV_PART_KNOB);  // the knob is the bright head
      lv_obj_set_style_radius(ringArc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
      lv_obj_set_style_pad_all(ringArc, 2, LV_PART_KNOB);
      break;
    case K_ORBIT:
      orbitRing = box(ringBox);
      lv_obj_set_size(orbitRing, 222, 222);
      lv_obj_center(orbitRing);
      lv_obj_set_style_radius(orbitRing, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(orbitRing, 2, 0);
      lv_obj_set_style_border_opa(orbitRing, LV_OPA_COVER, 0);
      orbitDot = mkDisc(ringBox, ORBIT_DOT);
      orbitExec(nullptr, 0);
      break;
    case K_DAYLIGHT:
      dayTrack = mkRingArc(6);
      lv_obj_set_style_arc_opa(dayTrack, LV_OPA_TRANSP, LV_PART_INDICATOR);
      dayArc = mkRingArc(6);
      lv_obj_add_flag(dayArc, LV_OBJ_FLAG_HIDDEN);
      sunDot = mkDisc(ringBox, 13);
      lv_obj_set_style_border_width(sunDot, 2, 0);  // black outline separates the dot from the arc
      lv_obj_set_style_border_color(sunDot, lv_color_black(), 0);
      lv_obj_set_style_border_opa(sunDot, LV_OPA_COVER, 0);
      lv_obj_add_flag(sunDot, LV_OBJ_FLAG_HIDDEN);
      break;
    default: break;
  }
}

// ---- Hourly chime: one comet lap around the ring at the top of the hour (day only, non-blocking) ----
static lv_timer_t *chimeTimer;
static int chimeStep;

static void chimeTick(lv_timer_t *) {
  chimeStep++;
  int tailEnd = chimeStep - TAIL_LEN - 1;  // ticks at or before this are behind the comet
  int lo = tailEnd - 1 < 0 ? 0 : tailEnd - 1;
  int hi = chimeStep < TICK_COUNT - 1 ? chimeStep : TICK_COUNT - 1;
  for (int i = lo; i <= hi; i++) styleTick(i, i > tailEnd ? chimeStep : ringSec);  // left behind: back to the real ring
  if (tailEnd >= TICK_COUNT) {
    lv_timer_delete(chimeTimer);
    chimeTimer = nullptr;
    for (int i = 0; i < TICK_COUNT; i++) styleTick(i, ringSec);
  }
}

static void startChime() {
  if (chimeTimer || nightMix != 0 || !opt.chime || !tickKind()) return;  // quiet night: no chime
  chimeStep = -1;
  chimeTimer = lv_timer_create(chimeTick, 16, nullptr);
}

// Moves the ring to `sec`. Nothing is drawn while the ring is hidden (night fully faded in).
static void ringUpdate(int sec) {
  if (sec == ringSec) return;
  int prev = ringSec;
  ringSec = sec;
  bool hidden = nightMix == 255;
  if (tickKind()) {
    if (hidden || chimeTimer) {
      // hidden, or the chime owns the ring (it restyles everything when done)
    } else if (prev < 0 || sec <= TAIL_LEN || sec < prev) {  // first draw, new minute, or last minute's ticks still dissolving
      for (int i = 0; i < TICK_COUNT; i++) styleTick(i, sec);
    } else {
      for (int i = prev > TAIL_LEN ? prev - TAIL_LEN : 0; i <= sec; i++) styleTick(i, sec);  // tail + skipped seconds
    }
  } else if (rk == K_ARC) {
    arcTo(sec, !hidden && prev >= 0);
  } else if (rk == K_ORBIT) {
    orbitTo(sec, !hidden && prev >= 0);
  }
}

// ---- Analog hands ----------------------------------------------------------------------------
static lv_obj_t *handH, *handM, *handS, *handCap, *handPin;
static lv_point_precise_t ptsH[2], ptsM[2], ptsS[2];  // must outlive the line objects

static void handSet(lv_obj_t *l, lv_point_precise_t *pts, float deg, float len, float back) {
  float x0, y0, x1, y1;
  polar(-back, deg, x0, y0);
  polar(len, deg, x1, y1);
  pts[0] = {(lv_value_precise_t)lroundf(x0), (lv_value_precise_t)lroundf(y0)};
  pts[1] = {(lv_value_precise_t)lroundf(x1), (lv_value_precise_t)lroundf(y1)};
  lv_line_set_points(l, pts, 2);
}

static void handsUpdate(const tm &t, bool minuteChanged) {
  if (!handH) return;
  if (minuteChanged) {
    handSet(handH, ptsH, (t.tm_hour % 12 + t.tm_min / 60.0f) * 30.0f, 54, 0);
    handSet(handM, ptsM, t.tm_min * 6.0f, 86, 0);
  }
  if (nightMix != 255) handSet(handS, ptsS, t.tm_sec * 6.0f, 98, 22);  // hidden at night: nothing moves
}

static void buildHands() {
  auto hand = [](int w) {
    lv_obj_t *l = lv_line_create(content);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
  };
  handH = hand(7);
  handM = hand(5);
  handS = hand(2);
  handCap = mkDisc(content, 12);
  lv_obj_center(handCap);
  handPin = mkDisc(content, 4);
  lv_obj_center(handPin);
  lv_obj_set_style_bg_color(handPin, lv_color_black(), 0);
  tm zero = {};
  handsUpdate(zero, true);
}

// ---- Retro seven-segment digits ----------------------------------------------------------------
static lv_obj_t *seg[4][7], *segColon[2];
static char segText[5] = "    ";  // HHMM, ' ' = blank digit
static constexpr int SEG_W = 30, SEG_H = 56, SEG_T = 6;

static void segPaint() {
  if (!seg[0][0]) return;
  static const uint8_t MAP[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};  // bit 0..6 = segment a..g
  const Theme &T = THEMES[opt.theme];
  lv_color_t on = mixDN(T.tick, nightC(100)), ghost = mixDN(scaleHex(T.tick, 9), 0x000000);
  for (int d = 0; d < 4; d++) {
    uint8_t m = isdigit((unsigned char)segText[d]) ? MAP[segText[d] - '0'] : 0;
    for (int s = 0; s < 7; s++) lv_obj_set_style_bg_color(seg[d][s], (m >> s) & 1 ? on : ghost, 0);
  }
  for (auto c : segColon) lv_obj_set_style_bg_color(c, on, 0);
}

static void buildSegments(int cy) {
  auto rect = [](int x, int y, int w, int h) {
    lv_obj_t *o = box(content);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
  };
  static const int xs[4] = {44, 80, 130, 166};
  int y = cy - SEG_H / 2, half = SEG_H / 2, vl = half - SEG_T / 2 - 3;
  for (int d = 0; d < 4; d++) {
    int x = xs[d], r = x + SEG_W - SEG_T;
    seg[d][0] = rect(x + 4, y, SEG_W - 8, SEG_T);                         // a: top
    seg[d][1] = rect(r, y + 4, SEG_T, vl);                                // b: top right
    seg[d][2] = rect(r, y + half + 2, SEG_T, vl);                         // c: bottom right
    seg[d][3] = rect(x + 4, y + SEG_H - SEG_T, SEG_W - 8, SEG_T);         // d: bottom
    seg[d][4] = rect(x, y + half + 2, SEG_T, vl);                         // e: bottom left
    seg[d][5] = rect(x, y + 4, SEG_T, vl);                                // f: top left
    seg[d][6] = rect(x + 4, y + half - SEG_T / 2, SEG_W - 8, SEG_T);      // g: middle
  }
  segColon[0] = rect(117, y + 15, 6, 6);
  segColon[1] = rect(117, y + SEG_H - 21, 6, 6);
}

// ---- Forecast: hourly rain-chance bars -----------------------------------------------------------
static lv_obj_t *bars[RAIN_BARS], *barLbl[RAIN_BARS], *barTemp[RAIN_BARS];
static constexpr int BAR_W = 12, BAR_PITCH = 18, BAR_MAX = 24, BAR_BASE = 191;  // BAR_BASE: y of the bars' bottom edge

// Dry spell (max chance < 30 %) with hourly temperatures known: the bars show temperature instead
// (scaled between the coolest and warmest of the six hours, cool blue to warm amber).
static bool barsTempMode() {
  int mx = -1, ok = 0;
  for (int k = 0; k < RAIN_BARS; k++) {
    if (rainPct[k] > mx) mx = rainPct[k];
    if (tempHr[k] != -32768) ok++;
  }
  return mx < 30 && ok == RAIN_BARS;
}

static void tempRange(int &lo, int &hi) {
  lo = 32767; hi = -32768;
  for (int k = 0; k < RAIN_BARS; k++) { if (tempHr[k] < lo) lo = tempHr[k]; if (tempHr[k] > hi) hi = tempHr[k]; }
}

static void barsPaint() {
  if (!bars[0]) return;
  bool tm = barsTempMode();
  int lo = 0, hi = 0;
  if (tm) tempRange(lo, hi);
  for (int k = 0; k < RAIN_BARS; k++) {
    int p = rainPct[k] < 0 ? 0 : rainPct[k] > 100 ? 100 : rainPct[k];
    uint32_t day = tm ? mixHex(0x5FA8FF, 0xFFB454, hi > lo ? (hi - tempHr[k]) * 100 / (hi - lo) : 50) : mixHex(0x6FB6FF, 0x24384F, p);
    bgc(bars[k], mixDN(day, nightC(tm ? 40 : 18 + p / 2)));
    tc(barLbl[k], mixDN(THEMES[opt.theme].colon, nightC(45)));
    tc(barTemp[k], mixDN(0xE6E6E6, nightC(70)));
  }
}

static void barsUpdate() {
  if (!bars[0]) return;
  bool tm = barsTempMode();
  int lo = 0, hi = 0;
  if (tm) tempRange(lo, hi);
  for (int k = 0; k < RAIN_BARS; k++) {
    bool show = rainStart >= 0;
    setHidden(bars[k], !show);
    setHidden(barLbl[k], !show);
    setHidden(barTemp[k], !show || !tm);
    if (!show) continue;
    int p = rainPct[k] < 0 ? 0 : rainPct[k] > 100 ? 100 : rainPct[k];
    int tmax = BAR_MAX - 10;  // leave room for the number above
    int h = tm ? 6 + (hi > lo ? (tempHr[k] - lo) * (tmax - 6) / (hi - lo) : (tmax - 6) / 2)
               : 3 + p * (BAR_MAX - 3) / 100;
    int x = 120 - (RAIN_BARS * BAR_PITCH - (BAR_PITCH - BAR_W)) / 2 + k * BAR_PITCH;
    lv_obj_set_size(bars[k], BAR_W, h);
    lv_obj_set_pos(bars[k], x, BAR_BASE - h);
    if (tm) {
      char tb[6];
      snprintf(tb, sizeof(tb), "%d", tempHr[k]);
      lv_label_set_text(barTemp[k], tb);
      lv_obj_update_layout(barTemp[k]);
      lv_obj_set_pos(barTemp[k], x + BAR_W / 2 - lv_obj_get_width(barTemp[k]) / 2, BAR_BASE - h - 17);
    }
    int hr = (rainStart + k) % 24;
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", opt.h24 ? hr : (hr % 12 == 0 ? 12 : hr % 12));
    lv_label_set_text(barLbl[k], buf);
    lv_obj_update_layout(barLbl[k]);
    lv_obj_set_pos(barLbl[k], x + BAR_W / 2 - lv_obj_get_width(barLbl[k]) / 2, BAR_BASE + 2);
  }
  barsPaint();
}

static void buildBars() {
  for (int k = 0; k < RAIN_BARS; k++) {
    bars[k] = box(content);
    lv_obj_set_style_radius(bars[k], 3, 0);
    lv_obj_set_style_bg_opa(bars[k], LV_OPA_COVER, 0);
    barLbl[k] = mkLabel(content, &manrope_caps_12);
    barTemp[k] = mkLabel(content, &manrope_caps_12);
  }
}

// ---- Shared pieces of a face ---------------------------------------------------------------------
static void mkTime(int y, const lv_font_t *font) {  // hours, colon, minutes in a centred row
  lv_obj_t *row = mkRow(content, 200, 0, y, 0);
  timeH = mkLabel(row, font);
  timeC = mkLabel(row, font);
  lv_label_set_text(timeC, ":");
  timeM = mkLabel(row, font);
  // The small AM/PM (and optional seconds) are NOT in the row (they would pull the digits off-centre);
  // placeAmpm() hangs them off the right of the minutes.
  floatAmpm = true;
  ampmLabel = mkLabel(content, &manrope_caps_12, 1);
  if (opt.seconds) secLabel = mkLabel(content, &manrope_14);
}

static void mkDateRow(int y, int gap, int spacing) {  // weekday + date side by side, small caps
  lv_obj_t *row = mkRow(content, 190, 0, y, gap);
  dayLabel = mkLabel(row, &manrope_caps_12, spacing);
  dateLabel = mkLabel(row, &manrope_caps_12, spacing);
}

// Vertical temperature range bar next to the temperature: high number at the top, low at the bottom.
static constexpr int RANGE_H = 34, RANGE_W = 4, RANGE_DOT = 10;

// Weather block: [icon] [temperature] and optionally today's range (hilo 1: bar with a dot at the
// current temperature + high/low numbers; 2: numbers only). column stacks icon over temperature.
static void mkWeather(int x, int y, bool column, int hilo, const lv_font_t *tempFont, const lv_font_t *iconFont) {
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
static void mkDetail(int x, int y) {
  detailBox = box(content);
  lv_obj_set_size(detailBox, 130, 18);
  lv_obj_align(detailBox, LV_ALIGN_CENTER, x, y);
  detailLabel = mkLabel(detailBox, &manrope_caps_12, 1);
  lv_obj_center(detailLabel);
}

// Offline marker: WiFi glyph with a slash through it.
static void mkOffline(int x, int y) {
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

// ---- The faces: which pieces, where (offsets are pixels from the centre, tuned for the round bezel) ----
static void buildFace() {
  switch (opt.face) {
    default:
    case FACE_POLISHED:  // date / time / weather with range bar / detail
      mkDateRow(-62, 7, 2);
      mkTime(0, &manrope_56);
      mkWeather(0, 62, false, 1, &manrope_20, &weather_icons);
      mkDetail(0, 90);
      mkOffline(0, -88);
      break;
    case FACE_ARC:  // two-line date, high/low as plain numbers
      dayLabel = mkLabelAt(&manrope_caps_12, 3, 0, -66);
      dateLabel = mkLabelAt(&manrope_caps_12, 2, 0, -49);
      mkTime(2, &manrope_56);
      mkWeather(0, 60, false, 2, &manrope_20, &weather_icons);
      mkDetail(0, 88);
      mkOffline(0, -88);
      break;
    case FACE_STACKED:  // hours over minutes; date in a column on the left, weather on the right
      timeH = mkLabelAt(&manrope_74, 0, 0, -33);
      timeM = mkLabelAt(&manrope_74, 0, 0, 33);
      {  // right-justify the hour over the minutes' units digit (matters for single-digit hours)
        lv_obj_set_width(timeH, 2 * lv_font_get_glyph_width(&manrope_74, 0x30, 0));
        lv_obj_set_style_text_align(timeH, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_letter_space(timeH, 8, 0);  // "1" is narrow, so the hour pair read tighter than the minutes
        lv_obj_align(timeH, LV_ALIGN_CENTER, 0, -33);
      }
      dayLabel = mkLabelAt(&manrope_caps_12, 2, -77, -21);
      date2Label = mkLabelAt(&manrope_20, 0, -77, 0);
      dateLabel = mkLabelAt(&manrope_caps_12, 2, -77, 21);
      mkWeather(77, -2, true, 0, &manrope_20, &weather_icons);
      ampmLabel = mkLabelAt(&manrope_caps_12, 2, 0, -90);
      if (opt.seconds) secLabel = mkLabelAt(&manrope_14, 0, 77, 40);
      mkDetail(0, 90);
      mkOffline(-77, -46);
      break;
    case FACE_ANALOG:  // hands; date window at 3 o'clock, weather at 6
      winBox = mkRow(content, 52, 55, 0, 5);
      lv_obj_set_height(winBox, 24);
      lv_obj_set_style_radius(winBox, 5, 0);
      lv_obj_set_style_border_width(winBox, 1, 0);
      lv_obj_set_style_border_opa(winBox, LV_OPA_COVER, 0);
      dayLabel = mkLabel(winBox, &manrope_caps_12);
      date2Label = mkLabel(winBox, &manrope_14);
      mkWeather(0, 52, false, 0, &manrope_20, &weather_icons);
      mkOffline(0, -54);
      buildHands();  // last, so the hands pass over everything else
      break;
    case FACE_ZEN:  // light digits and little else
      mkWeather(0, -54, false, 0, &manrope_20, &weather_icons);
      mkTime(0, &manrope_light_64);
      mkDateRow(50, 6, 2);
      mkOffline(0, -88);
      break;
    case FACE_FORECAST:  // weather in the centre, small time on top, hourly rain bars below
      mkDateRow(-85, 5, 1);
      mkTime(-59, &manrope_34);
      mkWeather(0, -6, false, 0, &manrope_34, &weather_icons_42);
      mkDetail(0, 30);
      buildBars();
      mkOffline(-66, -59);
      break;
    case FACE_RETRO:  // seven-segment LCD
      dayLabel = mkLabelAt(&manrope_14, 2, 0, -62);
      buildSegments(116);
      ampmLabel = mkLabelAt(&manrope_caps_12, 1, 64, 38);
      mkWeather(0, 64, false, 0, &manrope_20, &weather_icons);
      mkDetail(0, 90);
      mkOffline(0, -86);
      break;
    case FACE_DAYLIGHT:  // 24 h daylight dial around date / time / daylight left / weather
      mkDateRow(-60, 7, 2);
      mkTime(-10, &manrope_56);
      sunLabel = mkLabelAt(&manrope_caps_12, 1, 0, 25);
      mkWeather(0, 56, false, 0, &manrope_20, &weather_icons);
      mkDetail(0, 85);
      mkOffline(0, -84);
      break;
  }
}

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

// ---- Text transitions: the changed label fades out, swaps text, fades back in ----
struct Swap { lv_obj_t *label; char text[32]; };
enum { SW_HOURS, SW_MINUTES, SW_ICON, SW_TEMP, SW_HI, SW_LO, SW_DETAIL, SW_COUNT };
static Swap swaps[SW_COUNT];
static uint32_t iconDayColor = 0xFFFFFF;
static uint32_t pendingIconColor;  // applied when the icon glyph actually swaps

static void applyIconColor() {
  if (!weatherIcon) return;
  const Theme &T = THEMES[opt.theme];
  lv_color_t c = mixDN(iconDayColor, nightC(92));
  if (opt.face == FACE_ZEN) c = mixDN(T.sub, nightC(69));         // monochrome faces
  else if (opt.face == FACE_RETRO) c = mixDN(T.tick, nightC(100));
  lv_obj_set_style_text_color(weatherIcon, c, 0);
}

static void placeAmpm() {
  if (!floatAmpm || !timeM) return;
  lv_obj_update_layout(lv_obj_get_parent(timeM));  // digits changed width: re-place AM/PM beside them
  if (ampmLabel) lv_obj_align_to(ampmLabel, timeM, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, 3);
  if (secLabel) lv_obj_align_to(secLabel, timeM, LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
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

// Stacked hour: two digits centre over the minutes, a single digit right-justifies over their units digit.
static void hourAlign(lv_obj_t *l) {
  if (l != timeH || opt.face != FACE_STACKED) return;
  lv_obj_set_style_text_align(l, strlen(lv_label_get_text(l)) > 1 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, 0);
}

static void swapOutDone(lv_anim_t *a) {
  Swap *sw = (Swap *)lv_anim_get_user_data(a);
  lv_label_set_text(sw->label, sw->text);
  hourAlign(sw->label);
  if (sw->label == weatherIcon) {  // new glyph arrives with its own colour
    iconDayColor = pendingIconColor;
    applyIconColor();
  }
  placeAmpm();
  fadeIn(sw->label);
}

// Text change with a quick fade-out / fade-in. A hidden label just appears (fade-in only).
static void swapText(int idx, lv_obj_t *label, const char *text) {
  if (!label) return;
  if (lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
    lv_anim_delete(label, nullptr);
    lv_label_set_text(label, text);
    hourAlign(label);
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

// ---- Time and date ---------------------------------------------------------------------------
static void dateUpdate(const tm &t) {
  char wday[12], mon[12], buf[28], num[4];
  bool full = opt.face == FACE_ARC;  // "TUESDAY" / "NOVEMBER 10"
  strftime(wday, sizeof(wday), full ? "%A" : "%a", &t);
  strftime(mon, sizeof(mon), full ? "%B" : "%b", &t);
  upper(wday);
  upper(mon);
  snprintf(num, sizeof(num), "%d", t.tm_mday);  // %d in strftime would zero-pad single digits
  if (opt.dateFmt == 1) snprintf(buf, sizeof(buf), "%s %s", num, mon);
  else snprintf(buf, sizeof(buf), "%s %s", mon, num);
  switch (opt.face) {
    case FACE_STACKED:  // TUE / 10 / NOV down the left side
      setText(dayLabel, wday);
      setText(date2Label, num);
      setText(dateLabel, mon);
      break;
    case FACE_ANALOG:  // date window
      setText(dayLabel, wday);
      setText(date2Label, num);
      break;
    case FACE_RETRO: {  // "TUE 11-10" (or day first)
      char line[24];
      if (opt.dateFmt == 1) snprintf(line, sizeof(line), "%s %d-%d", wday, t.tm_mday, t.tm_mon + 1);
      else snprintf(line, sizeof(line), "%s %d-%d", wday, t.tm_mon + 1, t.tm_mday);
      setText(dayLabel, line);
      break;
    }
    case FACE_ZEN: case FACE_FORECAST: {  // one quiet line: "TUE · NOV 10"
      char line[12];
      snprintf(line, sizeof(line), "%s \xC2\xB7", wday);
      setText(dayLabel, line);
      setText(dateLabel, buf);
      break;
    }
    default:
      setText(dayLabel, wday);
      setText(dateLabel, buf);
  }
}

void ui_update(const tm &tIn) {
  tm t = tIn;  // may alias lastTm
  lastTm = t;
  if (!haveTm) {
    haveTm = true;
    lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
  }

  ringUpdate(t.tm_sec);
  if (secLabel) {
    char ss[4];
    snprintf(ss, sizeof(ss), "%02d", t.tm_sec);
    lv_label_set_text(secLabel, ss);
  }

  bool minuteChanged = t.tm_min != lastMin || t.tm_hour != lastHour;  // the hour alone can change (clock set)
  if (minuteChanged) {  // redraw the big text only when the minute changes
    bool first = lastMin < 0;
    lastMin = t.tm_min;
    lastHour = t.tm_hour;
    char hh[8], mm[8];
    if (opt.h24) snprintf(hh, sizeof(hh), "%02d", t.tm_hour);
    else snprintf(hh, sizeof(hh), "%d", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12);
    snprintf(mm, sizeof(mm), "%02d", t.tm_min);
    setText(ampmLabel, opt.h24 ? "" : (t.tm_hour < 12 ? "AM" : "PM"));
    if (seg[0][0]) {
      snprintf(segText, sizeof(segText), "%2s%s", hh, mm);  // leading hour digit blank in 12-hour mode
      segPaint();
    }
    if (first) {  // no transition on the very first draw
      setText(timeH, hh);
      hourAlign(timeH);
      setText(timeM, mm);
      placeAmpm();
    } else {
      swapText(SW_HOURS, timeH, hh);
      swapText(SW_MINUTES, timeM, mm);
      if (t.tm_min == 0) startChime();
    }
    daylightUpdate();
  }
  handsUpdate(t, minuteChanged);

  if (t.tm_yday != lastDay) {
    lastDay = t.tm_yday;
    dateUpdate(t);
  }
}

// At night the range (bar and numbers) is invisible but keeps its layout slot, and the row slides sideways
// in step with the fade so the icon + temperature end up centred (no jump when the range disappears).
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

// Recolours everything for the current nightMix, theme and night colour.
static void applyTheme() {
  const Theme &T = THEMES[opt.theme];
  bool retro = opt.face == FACE_RETRO, quiet = opt.face == FACE_ZEN || opt.face == FACE_FORECAST;
  lv_color_t digits = mixDN(T.digits, nightC(100)), accent = mixDN(T.accent, nightC(100)), sub = mixDN(T.sub, nightC(69));
  lv_color_t lcd = mixDN(T.tick, nightC(100)), lcdDim = mixDN(scaleHex(T.tick, 60), nightC(69));
  lv_obj_set_style_text_color(status, mixDN(0xFFFFFF, nightC(92)), 0);
  wifiPaint();  // these three only show during boot/setup and follow the target mode
  syncPaint();
  sonarPaint();
  if (!faceRoot) return;

  tc(timeH, digits);
  tc(timeM, opt.face == FACE_STACKED ? accent : digits);
  tc(timeC, opt.face == FACE_POLISHED ? mixDN(T.colon, nightC(60)) : digits);
  tc(ampmLabel, retro ? lcdDim : sub);
  tc(secLabel, sub);
  tc(dayLabel, retro ? lcdDim : quiet ? sub : accent);  // weekday: the accent
  tc(dateLabel, sub);
  tc(date2Label, digits);
  tc(sunLabel, sub);
  tc(weatherLabel, retro ? lcd : opt.face == FACE_ZEN ? sub : mixDN(scaleHex(T.digits, 85), nightC(92)));
  tc(detailLabel, retro ? lcdDim : sub);
  tc(offIcon, sub);
  if (offSlash) lv_obj_set_style_line_color(offSlash, sub, 0);
  if (winBox) lv_obj_set_style_border_color(winBox, mixDN(T.base, nightC(30)), 0);
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

  // Face extras.
  if (handH) {
    lv_obj_set_style_line_color(handH, digits, 0);
    lv_obj_set_style_line_color(handM, digits, 0);
    lv_obj_set_style_line_color(handS, mixDN(T.tick, 0x000000), 0);
    bgc(handCap, mixDN(T.tick, nightC(100)));
    setHidden(handS, nightMix == 255);
  }
  segPaint();
  barsPaint();

  // Quiet night: the ring fades to black along with everything else, then is hidden (no motion at all).
  // The analog dial's batons stay, dimmed, so the hands can be read.
  bool hideRing = nightMix == 255 && rk != K_MARKERS;
  setHidden(ringBox, hideRing);
  if (hideRing) return;
  for (int i = 0; i < TICK_COUNT; i++) styleTick(i, ringSec);
  if (ringArc) {
    lv_obj_set_style_arc_color(ringArc, mixDN(mixHex(T.dim, 0x000000, 60), 0x000000), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ringArc, mixDN(mixHex(T.tick, T.dim, 45), 0x000000), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ringArc, mixDN(T.tick, 0x000000), LV_PART_KNOB);
  }
  if (orbitRing) {
    lv_obj_set_style_border_color(orbitRing, mixDN(mixHex(T.dim, 0x000000, 70), 0x000000), 0);
    bgc(orbitDot, mixDN(T.tick, 0x000000));
  }
  daylightPaint();
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

// A comet (head + fading tail) runs once around the ring, leaving no trace.
void ui_boot_splash() {
  if (nightMix == 255 || !tickKind()) return;  // no ring at night; arc/dot/dial rings have no comet
  for (int step = 0; step < TICK_COUNT + TAIL_LEN + 2; step++) {
    int lo = step - TAIL_LEN - 1 < 0 ? 0 : step - TAIL_LEN - 1;
    int hi = step < TICK_COUNT - 1 ? step : TICK_COUNT - 1;
    for (int i = lo; i <= hi; i++) styleTick(i, i > step - TAIL_LEN - 1 ? step : -1);
    lv_timer_handler();
    delay(14);
  }
  ringSec = -1;
}

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

// A weather message with no icon ("ZIP not found") does not fit the side column or the big-digit
// temperature of these faces, so it goes on the detail line instead.
static bool msgInDetail() { return opt.face == FACE_STACKED || opt.face == FACE_FORECAST; }

// The detail line shows the weather message (see above), else the detail text if that option is on.
static void refreshDetail() {
  char buf[32] = "";
  if (wx.set && !wx.hasIcon && wx.text[0] && msgInDetail()) strlcpy(buf, wx.text, sizeof(buf));
  else if (opt.detail) strlcpy(buf, detailText, sizeof(buf));
  upper(buf);  // the small-caps font has no lowercase
  swapText(SW_DETAIL, detailLabel, buf);
}

void ui_set_weather_detail(const char *text) {
  strlcpy(detailText, text ? text : "", sizeof(detailText));
  refreshDetail();
}

static void setOpa(lv_obj_t *o, lv_opa_t to, uint32_t ms) {
  if (!o) return;
  lv_anim_delete(o, nullptr);
  if (!ms) {
    lv_obj_set_style_opa(o, to, 0);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t x) { lv_obj_set_style_opa((lv_obj_t *)v, (lv_opa_t)x, 0); });
  lv_anim_set_values(&a, lv_obj_get_style_opa(o, LV_PART_MAIN), to);
  lv_anim_set_duration(&a, ms);
  lv_anim_start(&a);
}

static void applyStale(uint32_t ms) {
  lv_opa_t to = staleShown ? 90 : LV_OPA_COVER;  // ~35%: still readable, clearly not current
  setOpa(weatherRow, to, ms);
  setOpa(detailBox, to, ms);
  for (int k = 0; k < RAIN_BARS; k++) { setOpa(bars[k], to, ms); setOpa(barTemp[k], to, ms); }
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

// Shows the cached weather. Updates fade (out, swap, in) instead of snapping; content appearing after
// the skeleton fades in. The range dot glides to its new position.
static void applyWeather(bool withFall) {
  if (!weatherRow || !wx.set) return;
  if (wx.hasIcon && withFall) startFall(wx.fall, wx.iconColor);
  // Temperatures use the face's own font; a message needs a full text font (and may go to the detail line).
  bool msg = !wx.hasIcon;
  lv_obj_set_style_text_font(weatherLabel, msg ? &manrope_14 : (const lv_font_t *)lv_obj_get_user_data(weatherLabel), 0);
  swapText(SW_TEMP, weatherLabel, msg && msgInDetail() ? "" : wx.text);
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
  barsUpdate();
  daylightUpdate();
}

// ---- Building and switching ------------------------------------------------------------------------
static void rebuild() {
  if (chimeTimer) { lv_timer_delete(chimeTimer); chimeTimer = nullptr; }
  lv_anim_delete(&rangeFrac, nullptr);
  lv_anim_delete(&orbitA, nullptr);
  lv_obj_clean(faceRoot);  // deletes every face object (and the animations running on them)
  timeH = timeC = timeM = ampmLabel = secLabel = dayLabel = dateLabel = date2Label = sunLabel = nullptr;
  weatherRow = weatherIcon = weatherLabel = skeleton = hiloBox = hiLabel = loLabel = rangeTrack = rangeDot = nullptr;
  detailBox = detailLabel = offBox = offIcon = offSlash = winBox = nullptr;
  ringArc = orbitRing = orbitDot = dayTrack = dayArc = sunDot = nullptr;
  handH = handM = handS = handCap = handPin = nullptr;
  memset(skelShape, 0, sizeof(skelShape));
  memset(drops, 0, sizeof(drops));
  memset(ticks, 0, sizeof(ticks));
  memset(seg, 0, sizeof(seg));
  memset(segColon, 0, sizeof(segColon));
  memset(bars, 0, sizeof(bars));
  memset(barLbl, 0, sizeof(barLbl));
  memset(barTemp, 0, sizeof(barTemp));
  memset(swaps, 0, sizeof(swaps));
  floatAmpm = false;
  lastMin = lastHour = lastDay = ringSec = -1;
  rangeSlot = 0;

  rk = ringKind();
  for (lv_obj_t **o : {&ringBox, &content}) {
    *o = box(faceRoot);
    lv_obj_set_size(*o, 240, 240);
  }
  buildRing();
  buildFace();
  applyTheme();

  // Fill the new face from the cached state.
  if (haveTm) ui_update(lastTm);
  else lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
  ui_set_weather_loading(loadingShown);
  if (!loadingShown) applyWeather(false);
  refreshDetail();
  barsUpdate();
  applyStale(0);
  if (offBox && offShown) {
    lv_obj_remove_flag(offBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(offBox, LV_OPA_COVER, 0);
  }
}

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  for (int &p : rainPct) p = -1;
  for (int &t : tempHr) t = -32768;

  faceRoot = box(scr);
  lv_obj_set_size(faceRoot, 240, 240);

  status = lv_label_create(scr);
  lv_obj_set_style_text_font(status, &manrope_20, 0);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_center(status);
  lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);

  createWifi(scr);
  createSync(scr);
  createSonar(scr);
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

void ui_set_face(int face) {
  opt.face = (uint8_t)(face >= 0 && face < FACE_COUNT ? face : 0);
  if (faceRoot) rebuild();
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
