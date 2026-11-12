#pragma once
#include <stdint.h>
#include <time.h>

void ui_init();
void ui_update(const tm &t);
void ui_set_status(const char *msg);  // nullptr hides the message
// Animated WiFi glyph (arcs light up in sequence) shown while connecting; replaces the status text.
void ui_set_connecting(bool on);
// Spinning clock + "Synchronizing" label shown while waiting for NTP time.
void ui_set_syncing(bool on);
// Sonar animation (expanding rings + rotating sweep) shown while scanning for WiFi networks.
void ui_set_scanning(bool on);
// Night mode: everything red, easy on the eyes in the dark. The first call (boot) applies instantly;
// later calls cross-fade the colours over ~45 s.
void ui_set_night(bool night);

// Clock faces. The setup page stores the index (NVS `face`); a short press of BOOT cycles through them.
enum Face {
  FACE_POLISHED,  // date / big time / weather with range bar (default)
  FACE_ARC,       // seconds as one sweeping arc, two-line date
  FACE_STACKED,   // hours over minutes, date and weather at the sides
  FACE_ANALOG,    // hands, date window, weather
  FACE_ZEN,       // light digits, one orbiting dot
  FACE_FORECAST,  // weather in the centre, hourly rain bars
  FACE_RETRO,     // seven-segment LCD
  FACE_DAYLIGHT,  // 24 h dial with the sunrise-to-sunset arc
  FACE_COUNT
};

// Display options from the setup page.
struct UiOptions {
  bool h24 = false;        // 24-hour clock
  uint8_t dateFmt = 0;     // 0 = "Nov 6", 1 = "6 Nov"
  uint8_t theme = 0;       // index into THEMES[] (src/ui/theme.cpp) (0 red, 1 amber, 2 mint, 3 ice, 4 orchid, 5 mono, 6 ember)
  uint8_t face = 0;        // Face
  uint8_t nightColor = 0;  // 0 red, 1 amber, 2 dim white
  bool chime = true;       // comet lap around the ring on the hour
  bool detail = true;      // "40% RAIN" / "FEELS 65" line
  bool seconds = false;    // seconds as small digits beside the time
};
// Applies the options and rebuilds the face. Call once at boot (after ui_init) before the splash.
void ui_set_options(const UiOptions &o);
// Switches the face in place (BOOT short press); everything shown is carried over.
void ui_set_face(int face);
int ui_face_count();
const char *ui_face_name(int face);  // "Polished", "Arc"... for the setup page
void ui_set_theme(int theme);  // colour theme index (BOOT double press cycles it)
int ui_theme_count();
const char *ui_theme_name(int theme);  // "Red", "Amber"...

// Boot splash: a comet sweeps once around the seconds ring (~1 s, blocking).
void ui_boot_splash();

// Spinner in the weather row's spot while the first fetch is in flight (hides the other row content).
void ui_set_weather_loading(bool loading);

// Row below the time: icon glyph (weather_icons font, UTF-8) + temperature text + a small vertical
// range bar (today's high at the top, low at the bottom, a dot at the current temperature).
// icon may be nullptr (text only, e.g. an error message); hasRange false hides the bar;
// text nullptr/"" clears the temperature.
// fall: 0 none, 1 rain, 2 snow -> a few drops/flakes drift down under the icon for a few seconds (day only).
void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, bool hasRange, int lo, int hi, float cur,
                    int fall = 0);

// Extra weather data some faces use: rain chance for the next `count` hours starting at local hour
// `startHour` (rain may be nullptr), and today's sunrise/sunset in minutes after midnight (-1 = unknown).
void ui_set_weather_extra(const int *rain, int count, int startHour, int sunriseMin, int sunsetMin,
                           const int *temps = nullptr);

// Wind for the compass ring (Forecast face): direction it comes FROM in degrees (-1 = unknown) and a
// ready-made readout such as "NW 12 MPH" ("" hides it), plus "Feels 65°" or "" (shown instead of the
// high/low on that face's detail line).
void ui_set_wind(int fromDeg, const char *text, const char *feels);

// Small line under the weather row, e.g. "40% rain" or "Feels 65°"; "" fades it away.
void ui_set_weather_detail(const char *text);

// Dims the weather when the last successful fetch is too old to trust.
void ui_set_weather_stale(bool stale);

// Small crossed-out WiFi glyph above the date while the network is down.
void ui_set_offline(bool offline);

// Replaces the clock face with the WiFi-setup screen (AP name + password).
void ui_show_setup(const char *apSsid, const char *apPass, const char *ip);
