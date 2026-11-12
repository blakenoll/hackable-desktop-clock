#pragma once
#include <Arduino.h>

// Everything the user can set, persisted in NVS (flash) via Preferences, namespace `clock`.
// The setup page (provisioning.cpp) writes it; main.cpp and weather.cpp read it.
struct ClockConfig {
  String ssid, pass, tz;
  String city;           // 5-digit US ZIP code for weather ("" = weather off)
  char units = 'F';      // 'F' or 'C'
  bool h24 = false;      // 24-hour clock (default 12-hour with AM/PM)
  uint8_t dateFmt = 0;   // 0 = "Fri Nov 6", 1 = "Fri 6 Nov"
  uint8_t accent = 0;    // colour theme: index into THEMES[] (src/ui/theme.cpp), 0 = red
  uint8_t face = 0;      // clock face (enum Face in ui.h); also changed by a short BOOT press
  uint8_t nightMode = 0; // 0 = after sunset (needs a ZIP), 1 = fixed hours, 2 = never
  uint8_t nightStart = 22, nightEnd = 7;  // fixed hours: night from nightStart:00 to nightEnd:00
  uint8_t nightColor = 0;  // 0 red, 1 amber, 2 dim white
  bool chime = true;     // hourly comet lap
  bool detail = true;    // rain chance / feels-like line
  bool seconds = false;  // seconds as digits
  String tideStation;    // NOAA tide station ID chosen on the setup page ("" = nearest to the ZIP)
  // Nearest tide station the tide code found for `tideGeo` (cached in NVS; "" = none within range).
  String tideGeo, tideAuto, tideName;
  // Coordinates the weather task resolved for `geocity` (cached in NVS).
  String geocity;
  float lat = 0, lon = 0;
};

// Loads saved settings into cfg. Returns true if a WiFi SSID has been saved.
bool config_load(ClockConfig &cfg);
// Saves everything the setup page edits (not the cached coordinates or the day/night state).
void config_save(const ClockConfig &cfg);

// Caches coordinates resolved for `city` so we only geocode when the city changes.
void config_save_geo(const String &city, float lat, float lon);
// Caches the nearest tide station found for `city` (id "" = none nearby), so the station list is only read once.
void config_save_tide_auto(const String &city, const String &id, const String &name);
// Last known day/night state (from the weather fetch), so the screen powers up in the right mode.
bool config_load_night();
void config_save_night(bool night);
// Single settings changed by a BOOT press.
void config_save_face(uint8_t face);
void config_save_theme(uint8_t theme);
