#pragma once
#include <Arduino.h>

// WiFi + timezone settings, persisted in NVS (flash) via Preferences.
struct ClockConfig {
  String ssid, pass, tz;
  String city;           // free-text place name for weather ("" = weather off)
  char units = 'F';      // 'F' or 'C'
  bool h24 = false;      // 24-hour clock (default 12-hour with AM/PM)
  uint8_t dateFmt = 0;   // 0 = "Fri Nov 6", 1 = "Fri 6 Nov"
  uint8_t accent = 0;    // theme preset: index into THEMES[] in ui.cpp (0 = red)
  uint8_t face = 0;      // clock face (enum Face in ui.h); also changed by a short BOOT press
  uint8_t ring = 0;      // 0 = the face's own ring, 1 ticks, 2 arc, 3 dot, 4 off
  uint8_t nightMode = 0; // 0 = after sunset (needs a ZIP), 1 = fixed hours, 2 = never
  uint8_t nightStart = 22, nightEnd = 7;  // fixed hours: night from nightStart:00 to nightEnd:00
  uint8_t nightColor = 0;  // 0 red, 1 amber, 2 dim white
  bool chime = true;     // hourly comet lap
  bool detail = true;    // rain chance / feels-like line
  bool seconds = false;  // seconds as digits
  // Coordinates the weather task resolved for `geocity` (cached in NVS).
  String geocity;
  float lat = 0, lon = 0;
};

// Caches coordinates resolved for `city` so we only geocode when the city changes.
void prov_save_geo(const String &city, float lat, float lon);

// Last known day/night state (from the weather fetch), so the screen powers up in the right mode.
bool prov_load_night();
void prov_save_night(bool night);

// Saves just the face (BOOT short press cycles it).
void prov_save_face(uint8_t face);

// Loads saved settings into cfg. Returns true if a WiFi SSID has been saved.
bool prov_load(ClockConfig &cfg);

// Poll often. Reports the BOOT button (GPIO 9): BTN_SHORT once when a brief press is released,
// BTN_LONG once when it has been held for ~3 s. (Holding BOOT *during reset* enters the ROM
// bootloader, so the press has to happen while the firmware is running.)
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };
ButtonEvent prov_button_event();
// Same poll, when only the long press matters (short presses are dropped).
bool prov_button_long_press();

// Starts a WPA2 access point with a random password, shows the network name and
// password on the display, and serves a captive-portal page for WiFi + timezone. Saves to
// NVS and reboots on submit; never returns.
// hasSaved: if true, reboots after a few idle minutes to retry saved settings.
[[noreturn]] void prov_run(bool hasSaved);
