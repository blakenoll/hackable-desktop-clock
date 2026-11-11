#include "ui_internal.h"

namespace ui {

// ---- Rings ---------------------------------------------------------------------------------
static RingKind rk = K_NONE;
static constexpr int TICK_COUNT = 60;
static lv_obj_t *ticks[TICK_COUNT];
static lv_point_precise_t tickPts[TICK_COUNT][2];  // must outlive the line objects
static int ringSec = -1;  // last second drawn on the ring (-1 = not drawn yet)
static lv_obj_t *ringArc, *orbitRing, *orbitDot;

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
lv_obj_t *mkRingArc(int width) {
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

void startChime() {
  if (chimeTimer || nightMix != 0 || !opt.chime || !tickKind()) return;  // quiet night: no chime
  chimeStep = -1;
  chimeTimer = lv_timer_create(chimeTick, 16, nullptr);
}

// Moves the ring to `sec`. Nothing is drawn while the ring is hidden (night fully faded in).
void ringUpdate(int sec) {
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

void ringBuild(RingKind kind) {
  rk = kind;
  ringSec = -1;
  memset(ticks, 0, sizeof(ticks));
  ringArc = orbitRing = orbitDot = nullptr;
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
    default: break;  // K_CUSTOM: the face builds it
  }
}

void ringStop() {
  if (chimeTimer) { lv_timer_delete(chimeTimer); chimeTimer = nullptr; }
  lv_anim_delete(&orbitA, nullptr);
}

void ringPaint(const Theme &T) {
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
}

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

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
