#pragma once
#include "config.h"

struct WeatherData {
  bool valid = false;     // true once a forecast has been fetched
  float temp = 0;
  int code = 0;           // WMO weather code
  bool isDay = true;
  bool loading = false;   // true from weather_begin() until the first result (or error) arrives
  bool hasRange = false;  // today's high/low available
  float hi = 0, lo = 0;
  float feels = 0;        // apparent ("feels like") temperature
  int rainPct = -1;       // highest precipitation probability over the next few hours, -1 if unknown
  static constexpr int RAIN_HOURS = 6;
  int rainHours[RAIN_HOURS] = {-1, -1, -1, -1, -1, -1};  // hourly precipitation probability, -1 if unknown
  int tempHours[RAIN_HOURS] = {-32768,-32768,-32768,-32768,-32768,-32768};  // hourly temperature (rounded), -32768 if unknown
  float windSpeed = 0;    // current wind speed (mph for F, km/h for C)
  int windDir = -1;       // direction the wind comes FROM, degrees clockwise from north, -1 if unknown
  int rainStartHour = -1; // local hour (0-23) of rainHours[0], -1 if unknown
  int sunriseMin = -1, sunsetMin = -1;  // today's sunrise/sunset, minutes after local midnight (-1 if unknown)
  uint32_t fetchedAt = 0; // millis() of the last successful fetch (to spot stale data)
  char unit = 'F';
  char error[24] = "";    // short message when there's no data to show
  uint32_t version = 0;   // bumps on every change; poll to know when to redraw
};

// Starts the background fetch task (geocodes cfg.city once, then refreshes the current conditions every
// 10 min via Open-Meteo). Call after WiFi is up. Does nothing if cfg.city is empty.
void weather_begin(const ClockConfig &cfg);

// Thread-safe snapshot of the latest data.
WeatherData weather_get();
