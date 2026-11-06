#pragma once
#include <Arduino.h>

// WiFi + timezone settings, persisted in NVS (flash) via Preferences.
struct ClockConfig {
  String ssid, pass, tz;
  String city;           // free-text place name for weather ("" = weather off)
  char units = 'F';      // 'F' or 'C'
  bool h24 = false;      // 24-hour clock (default 12-hour with AM/PM)
  uint8_t dateFmt = 0;   // 0 = "Fri Nov 6", 1 = "Fri 6 Nov"
  uint8_t accent = 0;    // index into ACCENTS[] in ui.cpp (0 = red)
  // Coordinates the weather task resolved for `geocity` (cached in NVS).
  String geocity;
  float lat = 0, lon = 0;
};

// Caches coordinates resolved for `city` so we only geocode when the city changes.
void prov_save_geo(const String &city, float lat, float lon);

// Last known day/night state (from the weather fetch), so the screen powers up in the right mode.
bool prov_load_night();
void prov_save_night(bool night);

// Loads saved settings into cfg. Returns true if a WiFi SSID has been saved.
bool prov_load(ClockConfig &cfg);

// Poll often. Returns true once when the BOOT button (GPIO 9) has been held
// for ~3 s. (Holding BOOT *during reset* enters the ROM bootloader, so the
// press has to happen while the firmware is running.)
bool prov_button_long_press();

// Starts a WPA2 access point with a random password, shows a join QR code on
// the display, and serves a captive-portal page for WiFi + timezone. Saves to
// NVS and reboots on submit; never returns.
// hasSaved: if true, reboots after a few idle minutes to retry saved settings.
[[noreturn]] void prov_run(bool hasSaved);
