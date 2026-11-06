#include "weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/semphr.h>
#include <math.h>

static constexpr uint32_t REFRESH_MS = 15UL * 60 * 1000;  // Open-Meteo "current" updates ~every 15 min
static constexpr uint32_t RETRY_MS = 60UL * 1000;

static WeatherData g_data;
static SemaphoreHandle_t g_mu;

static void publish(bool valid, float temp, int code, bool isDay, float hi, float lo, bool hasRange, char unit,
                    const char *err) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_data.valid = valid;
  g_data.temp = temp;
  g_data.code = code;
  g_data.isDay = isDay;
  g_data.hi = hi;
  g_data.lo = lo;
  g_data.hasRange = hasRange;
  g_data.loading = false;
  g_data.unit = unit;
  strlcpy(g_data.error, err ? err : "", sizeof(g_data.error));
  g_data.version++;
  xSemaphoreGive(g_mu);
}

WeatherData weather_get() {
  if (!g_mu) return WeatherData();  // weather disabled (no city configured)
  xSemaphoreTake(g_mu, portMAX_DELAY);
  WeatherData d = g_data;
  xSemaphoreGive(g_mu);
  return d;
}

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

// ---- HTTP helpers -----------------------------------------------------------

static String urlEncode(const String &s) {
  String o;
  char buf[4];
  for (char ch : s) {
    if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      o += ch;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)ch);
      o += buf;
    }
  }
  return o;
}

// HTTPS GET without certificate verification: this is public, read-only data
// and no credentials are sent, so we skip pinning a CA that could later rotate.
static bool httpsGet(const String &url, String &body) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  int status = http.GET();
  if (status == HTTP_CODE_OK) body = http.getString();
  http.end();
  return status == HTTP_CODE_OK;
}

// Parses the number following `key` (e.g. "\"latitude\":") at or after `from`.
static bool findNum(const String &s, const char *key, int from, double &out) {
  int i = s.indexOf(key, from);
  if (i < 0) return false;
  out = atof(s.c_str() + i + strlen(key));
  return true;
}

enum class Geo { Ok, NotFound, NetError };

static Geo geocode(const String &city, float &lat, float &lon) {
  String body;
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&countryCode=US&name=" + urlEncode(city);
  if (!httpsGet(url, body)) return Geo::NetError;
  int r = body.indexOf("\"results\"");
  double a, b;
  if (r < 0 || !findNum(body, "\"latitude\":", r, a) || !findNum(body, "\"longitude\":", r, b)) return Geo::NotFound;
  lat = (float)a;
  lon = (float)b;
  return Geo::Ok;
}

static bool fetchCurrent(float lat, float lon, char unit, float &temp, int &code, bool &isDay, float &hi, float &lo,
                         bool &hasRange) {
  String body;
  String url = "https://api.open-meteo.com/v1/forecast?current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto&temperature_unit=" +
               String(unit == 'C' ? "celsius" : "fahrenheit") + "&latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4);
  if (!httpsGet(url, body)) return false;
  int c = body.indexOf("\"current\":{");  // not "current_units"
  double t, w, d = 1;
  if (c < 0 || !findNum(body, "\"temperature_2m\":", c, t) || !findNum(body, "\"weather_code\":", c, w)) return false;
  findNum(body, "\"is_day\":", c, d);
  temp = (float)t;
  code = (int)w;
  isDay = d != 0;

  // Today's range. Search after "daily":{ so we skip the "daily_units" block.
  int dd = body.indexOf("\"daily\":{");
  double mx, mn;
  hasRange = dd >= 0 && findNum(body, "\"temperature_2m_max\":[", dd, mx) && findNum(body, "\"temperature_2m_min\":[", dd, mn);
  if (hasRange) {
    hi = (float)mx;
    lo = (float)mn;
  }
  return true;
}

// ---- Background task --------------------------------------------------------

static void weatherTask(void *) {
  for (;;) {
    ClockConfig cfg;
    prov_load(cfg);
    bool ok = false;

    if (WiFi.status() == WL_CONNECTED && cfg.city.length()) {
      bool haveCoords = cfg.geocity == cfg.city && (cfg.lat != 0 || cfg.lon != 0);
      if (!haveCoords) {
        float lat, lon;
        Geo g = geocode(cfg.city, lat, lon);
        if (g == Geo::Ok) {
          prov_save_geo(cfg.city, lat, lon);
          cfg.lat = lat;
          cfg.lon = lon;
          haveCoords = true;
        } else if (g == Geo::NotFound) {
          publish(false, 0, 0, true, 0, 0, false, cfg.units, "ZIP not found");
        } else if (!weather_get().valid) {
          publish(false, 0, 0, true, 0, 0, false, cfg.units, "No weather");
        }
      }
      if (haveCoords) {
        float temp;
        int code;
        bool isDay, hasRange;
        float hi = 0, lo = 0;
        if (fetchCurrent(cfg.lat, cfg.lon, cfg.units, temp, code, isDay, hi, lo, hasRange)) {
          publish(true, temp, code, isDay, hi, lo, hasRange, cfg.units, nullptr);
          ok = true;
        } else if (!weather_get().valid) {
          publish(false, 0, 0, true, 0, 0, false, cfg.units, "No weather");  // keep showing stale data if we have it
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(ok ? REFRESH_MS : RETRY_MS));
  }
}

void weather_begin(const ClockConfig &cfg) {
  if (cfg.city.length() == 0) return;
  g_mu = xSemaphoreCreateMutex();
  g_data.loading = true;  // show the spinner until the first fetch resolves
  g_data.version = 1;     // differs from the UI's initial 0, so it redraws right away
  xTaskCreate(weatherTask, "weather", 10240, nullptr, 1, nullptr);
}
