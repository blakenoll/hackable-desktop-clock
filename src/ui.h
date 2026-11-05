#pragma once
#include <time.h>

void ui_init();
void ui_update(const tm &t);
void ui_set_status(const char *msg);  // nullptr hides the message
// Replaces the clock face with the WiFi-setup screen (join QR + credentials).
void ui_show_setup(const char *apSsid, const char *apPass, const char *qrPayload, const char *ip);
