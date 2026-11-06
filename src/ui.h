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

// Row below the time: icon glyph (weather_icons font, UTF-8) + text + optional small stacked
// high/low. icon may be nullptr (text only, e.g. an error message); hi/lo nullptr hides them;
// text nullptr/"" clears the row.
void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, const char *hi, const char *lo);

// Replaces the clock face with the WiFi-setup screen (join QR + credentials).
void ui_show_setup(const char *apSsid, const char *apPass, const char *qrPayload, const char *ip);
