#pragma once
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

// Display options from the setup page: 24-hour clock, date order (0 = "Fri Nov 6", 1 = "Fri 6 Nov")
// and accent colour index (0 red, 1 amber, 2 green, 3 cyan, 4 pink, 5 white).
void ui_set_options(bool h24, int dateFmt, int accent);

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

// Small line under the weather row, e.g. "40% rain" or "Feels 65°"; "" fades it away.
void ui_set_weather_detail(const char *text);

// Dims the weather when the last successful fetch is too old to trust.
void ui_set_weather_stale(bool stale);

// Small crossed-out WiFi glyph above the date while the network is down.
void ui_set_offline(bool offline);

// Replaces the clock face with the WiFi-setup screen (AP name + password).
void ui_show_setup(const char *apSsid, const char *apPass, const char *ip);
