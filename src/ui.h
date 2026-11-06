#pragma once
#include <time.h>

void ui_init();
void ui_update(const tm &t);
void ui_set_status(const char *msg);  // nullptr hides the message
// Night mode: everything red, easy on the eyes in the dark.
void ui_set_night(bool night);

// Spinner in the weather row's spot while the first fetch is in flight (hides the other row content).
void ui_set_weather_loading(bool loading);

// Row below the time: icon glyph (weather_icons font, UTF-8) + text + optional small stacked
// high/low. icon may be nullptr (text only, e.g. an error message); hi/lo nullptr hides them;
// text nullptr/"" clears the row.
void ui_set_weather(const char *icon, uint32_t iconColor, const char *text, const char *hi, const char *lo);

// Replaces the clock face with the WiFi-setup screen (join QR + credentials).
void ui_show_setup(const char *apSsid, const char *apPass, const char *qrPayload, const char *ip);
