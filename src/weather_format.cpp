#include "weather_format.h"
#include <math.h>
#include <stdio.h>

// Glyphs from src/font_weather.c (Weather Icons, codepoints U+F0xx).
const char *weather_icon(int c, bool isDay, uint32_t *color) {
  if (c == 0) {
    *color = isDay ? 0xFFC83D : 0xC9D6FF;
    return isDay ? "\uF00D" : "\uF02E";  // sunny / night clear
  }
  if (c == 1 || c == 2) {
    *color = isDay ? 0xFFD66B : 0xC9D6FF;
    return isDay ? "\uF002" : "\uF086";  // day cloudy / night alt cloudy
  }
  *color = 0xB0B8C4;
  if (c == 3) return "\uF013";                        // cloudy
  if (c == 45 || c == 48) { *color = 0x9AA5B1; return "\uF014"; }  // fog
  if (c >= 51 && c <= 57) { *color = 0x6FB6FF; return "\uF01C"; }  // sprinkle
  if (c == 66 || c == 67) { *color = 0xA8D8FF; return "\uF0B5"; }  // sleet
  if (c >= 61 && c <= 65) { *color = 0x4DA3FF; return "\uF019"; }  // rain
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) { *color = 0xE6F2FF; return "\uF01B"; }  // snow
  if (c >= 80 && c <= 82) { *color = 0x4DA3FF; return "\uF01A"; }  // showers
  if (c >= 95) { *color = 0xFFD54F; return "\uF01E"; }              // thunderstorm
  return "\uF013";
}

int weather_fall(int c) {
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return 2;                // snow
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95) return 1;  // drizzle, rain, sleet, showers, storms
  return 0;
}

// Rain chance (max over the next hours) once it reaches this, else the "feels like" line.
static constexpr int RAIN_SHOW_PCT = 30;

void weather_feels_text(const WeatherData &w, char *out, size_t n) {
  out[0] = 0;
  if (fabsf(w.feels - w.temp) >= (w.unit == 'C' ? 3 : 5)) snprintf(out, n, "Feels %d\xC2\xB0", (int)lroundf(w.feels));
}

void weather_detail_text(const WeatherData &w, char *out, size_t n) {
  if (w.rainPct >= RAIN_SHOW_PCT) snprintf(out, n, "%d%% rain", w.rainPct);
  else weather_feels_text(w, out, n);
}

bool weather_wind_calm(const WeatherData &w) {
  return w.windSpeed < (w.unit == 'C' ? 5.0f : 3.0f);  // under ~3 mph the direction is mostly noise
}

void weather_wind_text(const WeatherData &w, char *out, size_t n) {
  static const char *POINTS[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  out[0] = 0;
  if (w.windDir < 0) return;
  int speed = (int)lroundf(w.windSpeed);
  if (weather_wind_calm(w)) snprintf(out, n, "CALM");
  else snprintf(out, n, "%s %d %s", POINTS[((w.windDir + 22) / 45) % 8], speed, w.unit == 'C' ? "KM/H" : "MPH");
}
