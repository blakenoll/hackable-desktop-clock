#include "weather.h"
#include "tide.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/semphr.h>
#include <math.h>

static constexpr uint32_t REFRESH_MS = 10UL * 60 * 1000;  // Open-Meteo "current" data itself updates ~every 15 min, so some polls return unchanged values
static constexpr uint32_t RETRY_MS = 60UL * 1000;
static constexpr int RAIN_HOURS = WeatherData::RAIN_HOURS;  // hourly values (rain chance, temperature) cover this many hours

static WeatherData g_data;
static SemaphoreHandle_t g_mu;

// Replaces the shared snapshot. A failed fetch is published as a default WeatherData with only `error` set
// (it is only published while there is no good data yet, so nothing is lost).
static void publish(WeatherData d) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  d.loading = false;
  d.version = g_data.version + 1;
  if (d.valid) d.fetchedAt = millis();
  g_data = d;
  xSemaphoreGive(g_mu);
}

static void publishError(char unit, const char *msg) {
  WeatherData d;
  d.unit = unit;
  strlcpy(d.error, msg, sizeof(d.error));
  publish(d);
}

WeatherData weather_get() {
  if (!g_mu) return WeatherData();  // weather disabled (no city configured)
  xSemaphoreTake(g_mu, portMAX_DELAY);
  WeatherData d = g_data;
  xSemaphoreGive(g_mu);
  return d;
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

// Reads up to `n` numbers from the JSON array that follows `key` (searched from `from`). Missing and null
// entries come back as NaN. Returns false if the array is not there.
static bool numberArray(const String &body, int from, const char *key, double *out, int n) {
  for (int k = 0; k < n; k++) out[k] = NAN;
  int at = from >= 0 ? body.indexOf(key, from) : -1;
  if (at < 0) return false;
  const char *p = body.c_str() + at + strlen(key);
  for (int k = 0; k < n && *p && *p != ']'; k++) {
    char *end;
    double v = strtod(p, &end);
    if (end != p) out[k] = v;
    const char *comma = strchr(end, ','), *close = strchr(end, ']');
    if (!comma || (close && close < comma)) break;  // that was the last entry
    p = comma + 1;
  }
  return true;
}

// One Open-Meteo call: current conditions, today's range and sun times, and the next RAIN_HOURS hourly values.
// Fills `d` (valid = true) on success.
static bool fetchCurrent(float lat, float lon, char unit, WeatherData &d) {
  String body;
  String url = "https://api.open-meteo.com/v1/forecast?current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m,wind_direction_10m"
               "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&hourly=precipitation_probability,temperature_2m&forecast_hours=" +
               String(RAIN_HOURS) + "&forecast_days=1&timezone=auto&temperature_unit=" +
               String(unit == 'C' ? "celsius" : "fahrenheit") + "&wind_speed_unit=" + String(unit == 'C' ? "kmh" : "mph") + "&latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4);
  if (!httpsGet(url, body)) return false;
  int c = body.indexOf("\"current\":{");  // not "current_units"
  double t, w, isDay = 1, feels, windSpeed, windDir;
  if (c < 0 || !findNum(body, "\"temperature_2m\":", c, t) || !findNum(body, "\"weather_code\":", c, w)) return false;
  findNum(body, "\"is_day\":", c, isDay);
  d = WeatherData();
  d.valid = true;
  d.unit = unit;
  d.temp = (float)t;
  d.code = (int)w;
  d.isDay = isDay != 0;
  d.feels = findNum(body, "\"apparent_temperature\":", c, feels) ? (float)feels : d.temp;
  d.windSpeed = findNum(body, "\"wind_speed_10m\":", c, windSpeed) ? (float)windSpeed : 0;
  d.windDir = findNum(body, "\"wind_direction_10m\":", c, windDir) ? ((int)lround(windDir) % 360 + 360) % 360 : -1;

  // Hourly rain chance (max over the slots feeds the detail line) and temperature (for the Forecast face).
  int hourly = body.indexOf("\"hourly\":{");
  double rain[RAIN_HOURS], temps[RAIN_HOURS];
  bool haveRain = numberArray(body, hourly, "\"precipitation_probability\":[", rain, RAIN_HOURS);
  numberArray(body, hourly, "\"temperature_2m\":[", temps, RAIN_HOURS);
  for (int k = 0; k < RAIN_HOURS; k++) {
    if (!isnan(rain[k])) {
      d.rainHours[k] = (int)rain[k];
      if (d.rainHours[k] > d.rainPct) d.rainPct = d.rainHours[k];
    }
    if (!isnan(temps[k])) d.tempHours[k] = (int)lround(temps[k]);
  }
  int startMin = isoMinutes(body, "\"time\":[\"", hourly);  // first hourly slot
  d.rainStartHour = haveRain && startMin >= 0 ? startMin / 60 : -1;

  // Today's range and sun times. Search after "daily":{ so we skip the "daily_units" block.
  int daily = body.indexOf("\"daily\":{");
  d.sunriseMin = isoMinutes(body, "\"sunrise\":[\"", daily);
  d.sunsetMin = isoMinutes(body, "\"sunset\":[\"", daily);
  double mx, mn;
  d.hasRange = daily >= 0 && findNum(body, "\"temperature_2m_max\":[", daily, mx) && findNum(body, "\"temperature_2m_min\":[", daily, mn);
  if (d.hasRange) {
    d.hi = (float)mx;
    d.lo = (float)mn;
  }
  return true;
}

// ---- Background task --------------------------------------------------------

// Fetches once. Returns true if the data was refreshed.
static bool refresh(const ClockConfig &cfg0) {
  ClockConfig cfg = cfg0;
  if (WiFi.status() != WL_CONNECTED || !cfg.city.length()) return false;
  bool haveCoords = cfg.geocity == cfg.city && (cfg.lat != 0 || cfg.lon != 0);
  if (!haveCoords) {
    float lat, lon;
    Geo g = geocode(cfg.city, lat, lon);
    if (g == Geo::Ok) {
      config_save_geo(cfg.city, lat, lon);
      cfg.lat = lat;
      cfg.lon = lon;
      haveCoords = true;
    } else if (g == Geo::NotFound) {
      publishError(cfg.units, "ZIP not found");
    } else if (!weather_get().valid) {
      publishError(cfg.units, "No weather");
    }
  }
  bool ok = false;
  if (haveCoords) {
    WeatherData d;
    ok = fetchCurrent(cfg.lat, cfg.lon, cfg.units, d);
    if (ok) publish(d);
    else if (!weather_get().valid) publishError(cfg.units, "No weather");  // keep showing stale data if we have it
  }
  tide_refresh(cfg);  // after the weather, so it never delays the first weather on screen; paces itself
  return ok;
}

static void weatherTask(void *) {
  for (;;) {
    ClockConfig cfg;
    config_load(cfg);
    vTaskDelay(pdMS_TO_TICKS(refresh(cfg) ? REFRESH_MS : RETRY_MS));
  }
}

void weather_begin(const ClockConfig &cfg) {
  if (cfg.city.length() == 0) return;
  g_mu = xSemaphoreCreateMutex();
  tide_begin();
  g_data.loading = true;  // show the skeleton until the first fetch resolves
  g_data.version = 1;     // differs from the UI's initial 0, so it redraws right away
  xTaskCreate(weatherTask, "weather", 10240, nullptr, 1, nullptr);
}
