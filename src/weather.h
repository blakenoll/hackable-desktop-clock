#pragma once
#include "provisioning.h"

struct WeatherData {
  bool valid = false;     // true once a forecast has been fetched
  float temp = 0;
  int code = 0;           // WMO weather code
  bool isDay = true;
  bool loading = false;   // true from weather_begin() until the first result (or error) arrives
  bool hasRange = false;  // today's high/low available
  float hi = 0, lo = 0;
  char unit = 'F';
  char error[24] = "";    // short message when there's no data to show
  uint32_t version = 0;   // bumps on every change; poll to know when to redraw
};

// Starts the background fetch task (geocodes cfg.city once, then refreshes the
// current conditions every 15 min via Open-Meteo). Call after WiFi is up.
// Does nothing if cfg.city is empty.
void weather_begin(const ClockConfig &cfg);

// Thread-safe snapshot of the latest data.
WeatherData weather_get();

// UTF-8 glyph in the `weather_icons` font for a WMO weather code, plus its colour (0xRRGGBB).
const char *weather_icon(int wmoCode, bool isDay, uint32_t *color);
