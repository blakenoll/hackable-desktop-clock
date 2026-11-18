#pragma once
// Private to src/setup/ (the setup-mode captive portal). Public entry point: portal.h.
//   portal.cpp   setup mode: WiFi scan, access point, DNS + web server, save and reboot
//   form.cpp     the setup page: HTML, one *Fields() helper per section, and reading the submitted form
//   zones.cpp    timezone presets (zones.h, also used by the settings API)
#include <Arduino.h>
#include <WebServer.h>
#include "../config.h"
#include "zones.h"

String html_esc(const String &s);
// One <option> for a scanned network (portal.cpp lists them strongest first); a lock marks secured ones.
String setup_network_option(const String &ssid, bool open);
// The whole setup page, pre-filled with `cur`, with the settings API key at the bottom.
String setup_page(const String &netOptions, const ClockConfig &cur, const String &apiToken);
// The page shown after saving.
String setup_saved_page(const String &ssid);
// Reads the submitted form into `n` (through config_fields(), so defaults and ranges match NVS).
void setup_parse(WebServer &server, ClockConfig &n);
