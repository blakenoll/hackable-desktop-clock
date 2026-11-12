#pragma once
#include <stddef.h>
#include <stdint.h>
#include "weather.h"

// How weather data is shown. Pure functions of the data: no fetching, no UI calls.

// UTF-8 glyph in the `weather_icons` font for a WMO weather code, plus its colour (0xRRGGBB).
const char *weather_icon(int wmoCode, bool isDay, uint32_t *color);

// Falling-precipitation animation for a WMO code: 0 none, 1 rain, 2 snow.
int weather_fall(int wmoCode);

// Short line under the weather: rain chance if it matters, else "feels like" if it differs noticeably.
// Empty when neither applies.
void weather_detail_text(const WeatherData &w, char *out, size_t n);

// "Feels 65°" when the apparent temperature differs noticeably (5 °F / 3 °C), else empty.
void weather_feels_text(const WeatherData &w, char *out, size_t n);

// Too light for the direction to mean much (the compass hides its arrow).
bool weather_wind_calm(const WeatherData &w);

// "NW 12 MPH" / "CALM" for the compass readout (the small font is caps-only); empty if the direction is unknown.
void weather_wind_text(const WeatherData &w, char *out, size_t n);
