#pragma once
#include <Arduino.h>

// WiFi + timezone settings, persisted in NVS (flash) via Preferences.
struct ClockConfig {
  String ssid, pass, tz;
};

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
