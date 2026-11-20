#pragma once
// Settings API over WiFi: JSON endpoints that read and change the settings in config_fields.h, so an app or a
// script can configure the clock without going through setup mode. Endpoints, auth and examples: docs/API.md.
#include <Arduino.h>
#include <WebServer.h>
#include "../config.h"

// Normal mode (after WiFi is up): serves the API on port 80 and advertises the clock over mDNS as
// `<api_hostname()>.local` with a `_deskclock._tcp` service. `cfg` is the live config (main.cpp's). After a
// change that doesn't need a restart is saved, `applied(prev)` runs with the old values so the caller can
// apply the new ones; changes that need a restart (WiFi, location, units) save and restart instead.
void api_begin(ClockConfig &cfg, void (*applied)(const ClockConfig &prev));
// Call from loop(): answers pending requests on the main loop, so `applied` may call LVGL.
void api_poll();

// Pairing (POST /api/pair, docs/API.md#pairing): while a pairing request waits for the BOOT button, a short press
// approves it and a double press cancels it. handleButton() in main.cpp asks this first and, if it returns true,
// treats the press as the answer and does nothing else with it. Main loop only.
bool api_pair_button(bool approve);
bool api_pair_pending();

// Setup mode: the same endpoints on the portal's server, without the token (joining the setup access point
// already needs the password from the display). GET /api/config also returns the token there, and a saved
// change always restarts the clock.
void api_setup_routes(WebServer &server, ClockConfig &cfg);

// "A1B2": the last two bytes of this chip's MAC, so several clocks get distinct, stable names.
String api_device_suffix();
// "a0b1c2d3e4f5": the whole MAC in lowercase hex, a stable unique ID (GET /api/info, mDNS TXT `id`).
String api_device_id();
// "deskclock-a1b2": the DHCP hostname and mDNS name (API_HOSTNAME_PREFIX in tuning.h + the suffix).
String api_hostname();
