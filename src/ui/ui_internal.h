// Private header shared by the files in src/ui/. The public API is ../ui.h; nothing outside src/ui includes this.
//
// Layout of the UI code:
//   theme.cpp          options, colour themes, night colours and the day/night blend
//   widgets.cpp        small LVGL helpers (boxes, labels, rows, null-safe setters)
//   transitions.cpp    text fade-out / swap / fade-in
//   ring.cpp           the generic seconds rings (ticks, arc, orbit), the hourly chime and the boot splash
//   time_view.cpp      time / date labels, the pieces faces assemble (mkTime...), ui_update() and the date text
//   weather_view.cpp   weather row, skeleton, detail line, offline marker, rain particles, hourly data
//   boot_screens.cpp   connecting / syncing / scanning animations, status text, setup screen
//   core.cpp           palette + night theming, rebuild(), init and option setters
//   faces.cpp          the FaceDef table;  faces/face_*.cpp: one file per clock face
#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <ctype.h>
#include <initializer_list>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../ui.h"

LV_FONT_DECLARE(weather_icons);
LV_FONT_DECLARE(weather_icons_42);
LV_FONT_DECLARE(manrope_14);
LV_FONT_DECLARE(manrope_20);
LV_FONT_DECLARE(manrope_34);
LV_FONT_DECLARE(manrope_56);
LV_FONT_DECLARE(manrope_74);
LV_FONT_DECLARE(manrope_light_64);
LV_FONT_DECLARE(manrope_caps_12);  // bold small caps: 0x20-0x5F (no lowercase), degree sign, middle dot

namespace ui {

struct Palette;
struct DateParts;

// ---- theme.cpp ---------------------------------------------------------------------------------
// Theme presets (setup page; NVS key `accent`, so the first six keep the old accent order).
// digits: time; accent: weekday etc.; tick: current-second mark; sub: secondary text; colon: dim text;
// base / bright / dim: seconds-ring greys (elapsed, comet tail, upcoming).
struct Theme { uint32_t digits, accent, tick, sub, colon, base, bright, dim; };
extern const Theme THEMES[];
extern const int THEME_COUNT;
extern const int NIGHT_COUNT;

extern UiOptions opt;
extern bool night;        // target mode (what ui_set_night last asked for)
extern uint8_t nightMix;  // current blend: 0 = full day colours, 255 = full night (animated)

uint32_t scaleHex(uint32_t c, int pct);
uint32_t mixHex(uint32_t a, uint32_t b, int pctA);  // pctA = 100 -> a
uint32_t nightC(int pct);                           // the night colour at `pct` brightness
lv_color_t mixDN(uint32_t day, uint32_t nt);        // day/night pair blended by nightMix

// ---- widgets.cpp -------------------------------------------------------------------------------
constexpr float RING_CENTER = 119.5f;  // display is 240x240

lv_obj_t *box(lv_obj_t *parent);
lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, int spacing = 0);
lv_obj_t *mkRow(lv_obj_t *parent, int w, int x, int y, int gap, bool column = false);
lv_obj_t *mkDisc(lv_obj_t *parent, int size);
void tc(lv_obj_t *o, lv_color_t c);  // null-safe text colour
void bgc(lv_obj_t *o, lv_color_t c); // null-safe background colour
void setText(lv_obj_t *o, const char *t);
void setHidden(lv_obj_t *o, bool hide);
void upper(char *s);
void polar(float r, float deg, float &x, float &y);  // `r` px from the centre, `deg` clockwise from 12 o'clock
void setOpa(lv_obj_t *o, lv_opa_t to, uint32_t ms);  // animated (or instant with ms == 0) opacity

// ---- Face containers and cached model ------------------------------------------------------------
// Everything on the clock face lives under faceRoot and is torn down and rebuilt when the face (or an
// option) changes: ringBox holds the ring, content holds the rest and stays hidden until the first
// ui_update() so the boot animations have the centre to themselves. A face only creates the objects it
// uses, so every pointer below may be null: code that touches them goes through the null-safe helpers
// or checks first. What is shown (time, weather...) is cached and re-applied after a rebuild.
extern lv_obj_t *faceRoot, *ringBox, *content;

// time_view.cpp
extern lv_obj_t *timeH, *timeC, *timeM, *ampmLabel, *secLabel, *dayLabel, *dateLabel, *date2Label;
extern bool floatAmpm;  // AM/PM (and the seconds digits) hang off the right of the minutes
extern tm lastTm;
extern bool haveTm;
lv_obj_t *mkLabelAt(const lv_font_t *font, int spacing, int x, int y);  // centred-offset label in `content`
void mkTime(int y, const lv_font_t *font);                              // hours, colon, minutes in a centred row
void mkDateRow(int y, int gap, int spacing);                            // weekday + date side by side, small caps
void fmtHour(const tm &t, char *out, size_t n);                         // "7" in 12-hour mode, "07" in 24-hour
void dateQuietLine(const DateParts &d);                                 // one quiet line: "TUE · NOV 10"
void timeReset();                                                       // forget the pointers and caches of the old face

// weather_view.cpp
extern lv_obj_t *weatherRow, *weatherIcon, *weatherLabel, *skeleton, *skelShape[5];
extern lv_obj_t *hiloBox, *hiLabel, *loLabel, *rangeTrack, *rangeDot;
extern lv_obj_t *detailBox, *detailLabel, *offBox, *offIcon, *offSlash;
constexpr int RAIN_BARS = 6;
extern int rainPct[RAIN_BARS], tempHr[RAIN_BARS], rainStart;  // hourly rain chance / temperature from the hour `rainStart` (-1 = none)
extern int sunriseMin, sunsetMin;                              // minutes after midnight (-1 = unknown)
void mkWeather(int x, int y, bool column, int hilo, const lv_font_t *tempFont, const lv_font_t *iconFont);
void mkDetail(int x, int y);
void mkOffline(int x, int y);
void refreshDetail();
void weatherReset();    // forget the pointers of the old face
void weatherPaint(const Palette &p);  // recolour the weather row, detail line, range bar, skeleton, offline marker
void weatherRestore();  // re-apply the cached weather / detail / stale / offline state to a new face

// ---- transitions.cpp -----------------------------------------------------------------------------
enum { SW_HOURS, SW_MINUTES, SW_ICON, SW_TEMP, SW_HI, SW_LO, SW_DETAIL, SW_COUNT };
extern uint32_t iconDayColor;
extern uint32_t pendingIconColor;  // applied when the icon glyph actually swaps
void swapText(int idx, lv_obj_t *label, const char *text);  // fade out, swap, fade in (a hidden label just fades in)
void fadeIn(lv_obj_t *o);
void placeAmpm();
void transitionsReset();

// ---- ring.cpp ------------------------------------------------------------------------------------
// Ticks: 60 line ticks with a comet tail. DotTicks: same, minute ticks drawn as dots. Blocks: chunky
// LCD-style blocks that fill up. Markers: static analog-dial batons. Arc: one arc that grows with the
// seconds. Orbit: a single dot on a hairline circle. Custom: the face builds its own ring (compass,
// daylight dial) in ringBox from FaceDef::build.
enum RingKind { K_NONE, K_TICKS, K_DOTTICKS, K_BLOCKS, K_MARKERS, K_ARC, K_ORBIT, K_CUSTOM };
lv_obj_t *mkRingArc(int width);  // bare arc (no knob, not clickable) filling the ring; angle 0 is 12 o'clock
void ringStop();                 // stop the chime and ring animations (before the face is torn down)
void ringBuild(RingKind kind);   // creates the generic ring in ringBox
void ringUpdate(int sec);        // moves the ring to `sec`
void ringPaint(const Theme &T);  // recolours the generic ring
void startChime();

// ---- boot_screens.cpp ----------------------------------------------------------------------------
void bootInit(lv_obj_t *scr);  // creates the status text and the connecting / syncing / scanning widgets
void bootPaint();              // recolours them for the current night mode

// ---- core.cpp: palette, theming, rebuild ---------------------------------------------------------
// Text colours for the current theme / night blend. Every face starts from the same defaults and may
// override some (FaceDef::palette).
struct Palette {
  lv_color_t digits, accent, sub;
  lv_color_t timeH, timeC, timeM, ampm, sec, day, date, date2, weather, detail, icon;
};
Palette makePalette();
void applyIconColor();
void applyTheme();

// ---- Faces ---------------------------------------------------------------------------------------
// The date, split into the pieces faces arrange differently.
struct DateParts {
  const tm &t;
  char wday[12], mon[12], num[4];  // "TUE", "NOV", "10" (full names when FaceDef::fullDate)
  char line[28];                   // "NOV 10" or "10 NOV" per the date-format option
};

// Everything that makes one face different. A face file defines one of these (plus whatever private
// state and helpers it needs, usually via a small make() function); faces.cpp lists them in `enum Face`
// order. Hooks may be null.
struct FaceDef {
  RingKind ring = K_NONE;
  void (*build)() = nullptr;                                  // creates the ring extras and content objects (see mkTime etc.)
  void (*date)(const DateParts &d) = nullptr;                 // lay the date out; default: weekday + date labels
  void (*palette)(Palette &p, const Theme &T) = nullptr;      // override default text colours
  void (*paint)(const Theme &T, const Palette &p) = nullptr;  // recolour face-private objects
  void (*paintRing)(const Theme &T) = nullptr;                // recolour a custom ring (skipped while the ring is hidden)
  void (*onTime)(const tm &t, bool minuteChanged) = nullptr;  // every ui_update(), after the time text
  void (*onData)() = nullptr;                                 // hourly / sun / wind data changed, or the face was rebuilt
  void (*onText)(lv_obj_t *label) = nullptr;                  // a label's text was just set
  void (*onStale)(lv_opa_t to, uint32_t ms) = nullptr;        // dim face-private weather objects
  const char *(*detailPrefix)() = nullptr;                    // text shown before the detail ("NW 12 MPH"), or null
  bool msgInDetail = false;   // a weather message with no icon ("ZIP not found") goes on the detail line
  bool fullDate = false;      // "TUESDAY" / "NOVEMBER" instead of "TUE" / "NOV"
  bool ringAtNight = false;   // the ring stays (dimmed) at night instead of fading out
};
const FaceDef &face();  // the active face (faces.cpp)

}  // namespace ui
