#include "weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/semphr.h>
#include <math.h>

static constexpr uint32_t REFRESH_MS = 10UL * 60 * 1000;  // Open-Meteo "current" data itself updates ~every 15 min, so some polls return unchanged values
static constexpr uint32_t RETRY_MS = 60UL * 1000;

static WeatherData g_data;
static SemaphoreHandle_t g_mu;

struct Extra {
  float feels = 0;
  int rainPct = -1;
  int rainHours[WeatherData::RAIN_HOURS] = {-1, -1, -1, -1, -1, -1};
  int tempHours[WeatherData::RAIN_HOURS] = {-32768, -32768, -32768, -32768, -32768, -32768};
  float windSpeed = 0;
  int windDir = -1;
  int rainStartHour = -1, sunriseMin = -1, sunsetMin = -1;
};

static void publish(bool valid, float temp, int code, bool isDay, float hi, float lo, bool hasRange, char unit,
                    const char *err, const Extra &ex = Extra()) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  if (valid) {
    g_data.feels = ex.feels;
    g_data.rainPct = ex.rainPct;
    memcpy(g_data.rainHours, ex.rainHours, sizeof(g_data.rainHours));
    memcpy(g_data.tempHours, ex.tempHours, sizeof(g_data.tempHours));
    g_data.windSpeed = ex.windSpeed;
    g_data.windDir = ex.windDir;
    g_data.rainStartHour = ex.rainStartHour;
    g_data.sunriseMin = ex.sunriseMin;
    g_data.sunsetMin = ex.sunsetMin;
    g_data.fetchedAt = millis();
  }
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

int weather_fall(int c) {
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return 2;                // snow
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95) return 1;  // drizzle, rain, sleet, showers, storms
  return 0;
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

static constexpr int RAIN_HOURS = WeatherData::RAIN_HOURS;  // rain chance looks this many hours ahead

// Minutes after midnight from the first ISO time ("2026-11-10T06:42") in the array following `key`, or -1.
static int isoMinutes(const String &s, const char *key, int from) {
  int i = from >= 0 ? s.indexOf(key, from) : -1;
  if (i < 0) return -1;
  int t = s.indexOf('T', i + strlen(key));
  if (t < 0 || t - i > (int)strlen(key) + 12 || t + 5 >= (int)s.length()) return -1;
  const char *p = s.c_str() + t + 1;
  if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) || p[2] != ':' || !isdigit((unsigned char)p[3]) ||
      !isdigit((unsigned char)p[4]))
    return -1;
  return ((p[0] - '0') * 10 + (p[1] - '0')) * 60 + (p[3] - '0') * 10 + (p[4] - '0');
}

static bool fetchCurrent(float lat, float lon, char unit, float &temp, int &code, bool &isDay, float &hi, float &lo,
                         bool &hasRange, Extra &ex) {
  String body;
  String url = "https://api.open-meteo.com/v1/forecast?current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m,wind_direction_10m"
               "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&hourly=precipitation_probability,temperature_2m&forecast_hours=" +
               String(RAIN_HOURS) + "&forecast_days=1&timezone=auto&temperature_unit=" +
               String(unit == 'C' ? "celsius" : "fahrenheit") + "&wind_speed_unit=" + String(unit == 'C' ? "kmh" : "mph") + "&latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4);
  if (!httpsGet(url, body)) return false;
  int c = body.indexOf("\"current\":{");  // not "current_units"
  double t, w, d = 1;
  if (c < 0 || !findNum(body, "\"temperature_2m\":", c, t) || !findNum(body, "\"weather_code\":", c, w)) return false;
  findNum(body, "\"is_day\":", c, d);
  temp = (float)t;
  code = (int)w;
  isDay = d != 0;
  double fl;
  ex.feels = findNum(body, "\"apparent_temperature\":", c, fl) ? (float)fl : temp;

  double wsp, wdir;
  ex.windSpeed = findNum(body, "\"wind_speed_10m\":", c, wsp) ? (float)wsp : 0;
  ex.windDir = findNum(body, "\"wind_direction_10m\":", c, wdir) ? ((int)lround(wdir) % 360 + 360) % 360 : -1;

  // Rain chance: max over the next RAIN_HOURS hourly values (entries may be null).
  ex.rainPct = -1;
  int hr = body.indexOf("\"hourly\":{");
  int arr = hr >= 0 ? body.indexOf("\"precipitation_probability\":[", hr) : -1;
  if (arr >= 0) {
    const char *p = body.c_str() + arr + strlen("\"precipitation_probability\":[");
    for (int n = 0; n < RAIN_HOURS && *p && *p != ']'; n++) {
      char *end;
      long v = strtol(p, &end, 10);
      if (end != p) {
        ex.rainHours[n] = (int)v;
        if (v > ex.rainPct) ex.rainPct = (int)v;
      }
      const char *comma = strchr(end, ','), *close = strchr(end, ']');
      if (!comma || (close && close < comma)) break;  // that was the last entry
      p = comma + 1;
    }
  }

  // Hourly temperature (same slots), for the Forecast face when no rain is expected.
  int tarr = hr >= 0 ? body.indexOf("\"temperature_2m\":[", hr) : -1;
  if (tarr >= 0) {
    const char *p = body.c_str() + tarr + strlen("\"temperature_2m\":[");
    for (int n = 0; n < RAIN_HOURS && *p && *p != ']'; n++) {
      char *end;
      double v = strtod(p, &end);
      if (end != p) ex.tempHours[n] = (int)lround(v);
      const char *comma = strchr(end, ','), *close = strchr(end, ']');
      if (!comma || (close && close < comma)) break;
      p = comma + 1;
    }
  }

  int startMin = isoMinutes(body, "\"time\":[\"", hr);  // first hourly slot
  ex.rainStartHour = arr >= 0 && startMin >= 0 ? startMin / 60 : -1;

  // Today's range. Search after "daily":{ so we skip the "daily_units" block.
  int dd = body.indexOf("\"daily\":{");
  ex.sunriseMin = isoMinutes(body, "\"sunrise\":[\"", dd);
  ex.sunsetMin = isoMinutes(body, "\"sunset\":[\"", dd);
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
        Extra ex;
        if (fetchCurrent(cfg.lat, cfg.lon, cfg.units, temp, code, isDay, hi, lo, hasRange, ex)) {
          publish(true, temp, code, isDay, hi, lo, hasRange, cfg.units, nullptr, ex);
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
  g_data.loading = true;  // show the skeleton until the first fetch resolves
  g_data.version = 1;     // differs from the UI's initial 0, so it redraws right away
  xTaskCreate(weatherTask, "weather", 10240, nullptr, 1, nullptr);
}
