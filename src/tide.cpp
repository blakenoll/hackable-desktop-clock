#include "tide.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/semphr.h>
#include <math.h>

static constexpr uint32_t TIDE_REFRESH_MS = 6UL * 60 * 60 * 1000;  // predictions don't change; this just keeps the window current
static constexpr float MAX_STATION_KM = 60;                         // farther than this: no tides here (inland)

static TideData g_tide;
static SemaphoreHandle_t g_mu;
static constexpr uint32_t WATER_REFRESH_MS = 30UL * 60 * 1000;
static uint32_t g_at = 0;       // millis() of the last fetch that settled the data (0 = never)
static uint32_t g_waterAt = 0;  // same for the water temperature
static String g_for;       // station + unit that data is for

void tide_begin() { g_mu = xSemaphoreCreateMutex(); }

TideData tide_get() {
  if (!g_mu) return TideData();
  xSemaphoreTake(g_mu, portMAX_DELAY);
  TideData d = g_tide;
  xSemaphoreGive(g_mu);
  return d;
}

static void publish(TideData d) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  d.version = g_tide.version + 1;
  g_tide = d;
  xSemaphoreGive(g_mu);
}

static void publishError(char unit, const char *msg) {
  TideData d;
  d.unit = unit;
  strlcpy(d.error, msg, sizeof(d.error));
  publish(d);
}

// ---- Nearest station --------------------------------------------------------
// NOAA's list of tide-prediction stations is ~2 MB of JSON, far too big to hold, so it is parsed as it streams
// in (HTTPClient::writeToStream also undoes chunked encoding). Each station object has "id", "name", "lat"
// and "lng" in that order; we keep the closest.
class StationFinder : public Stream {
 public:
  StationFinder(float lat, float lon) : lat0(lat), lon0(lon), coslat(cosf(lat * (float)M_PI / 180)) {}
  int count = 0;
  float bestKm = 1e9f;
  char bestId[12] = "", bestName[32] = "";

  size_t write(uint8_t c) override {
    switch (st) {
      case KEY: {
        memmove(tail, tail + 1, sizeof(tail) - 2);
        tail[sizeof(tail) - 2] = (char)c;
        if (ends("\"id\":")) begin(ID);
        else if (ends("\"name\":")) begin(NAME);
        else if (ends("\"lat\":")) begin(LAT);
        else if (ends("\"lng\":")) begin(LNG);
        break;
      }
      case ID: case NAME:
        if (!started) { if (c == '"') started = true; else if (c != ' ') st = KEY; break; }
        if (esc) { esc = false; add(c); break; }
        if (c == '\\') { esc = true; break; }
        if (c == '"') { finish(); break; }
        add(c);
        break;
      case LAT: case LNG:
        if (isdigit(c) || c == '-' || c == '.' || c == 'e' || c == 'E' || c == '+') { add(c); break; }
        if (c == ' ' && !len) break;
        finish();
        break;
    }
    return 1;
  }
  size_t write(const uint8_t *b, size_t n) override {
    for (size_t i = 0; i < n; i++) write(b[i]);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  enum State { KEY, ID, NAME, LAT, LNG } st = KEY;
  char tail[9] = "";  // last 8 characters while looking for a key
  char val[32];
  int len = 0;
  bool started = false, esc = false;
  char id[12] = "", name[32] = "";
  float lat = NAN;
  float lat0, lon0, coslat;

  bool ends(const char *k) {
    size_t n = strlen(k);
    return strncmp(tail + sizeof(tail) - 1 - n, k, n) == 0;
  }
  void begin(State s) { st = s; len = 0; started = false; esc = false; memset(tail, 0, sizeof(tail)); }
  void add(uint8_t c) { if (len < (int)sizeof(val) - 1) val[len++] = (char)c; }
  void finish() {
    val[len] = 0;
    switch (st) {
      case ID: strlcpy(id, val, sizeof(id)); lat = NAN; break;
      case NAME: strlcpy(name, val, sizeof(name)); break;
      case LAT: lat = len ? atof(val) : NAN; break;  // empty: "lat": null
      case LNG:
        if (!isnan(lat) && id[0] && len) {
          count++;
          float dx = (atof(val) - lon0) * coslat * 111.32f, dy = (lat - lat0) * 110.57f;
          float km = sqrtf(dx * dx + dy * dy);
          if (km < bestKm) { bestKm = km; strlcpy(bestId, id, sizeof(bestId)); strlcpy(bestName, name, sizeof(bestName)); }
        }
        id[0] = 0;
        break;
      default: break;
    }
    st = KEY;
  }
};

enum class Find { Ok, None, NetError };

static Find findStation(float lat, float lon, String &id, String &name) {
  WiFiClientSecure client;
  client.setInsecure();  // public, read-only data (same reasoning as weather.cpp)
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, "https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions")) return Find::NetError;
  int status = http.GET();
  StationFinder f(lat, lon);
  int got = status == HTTP_CODE_OK ? http.writeToStream(&f) : -1;
  int size = http.getSize();
  http.end();
  // A cut-off download would pick the nearest of the stations seen so far: insist on (nearly) all of them.
  if (got <= 0 || (size > 0 && got < size) || f.count < 1000) return Find::NetError;
  Serial.printf("tide: %d stations, nearest %s %s (%.0f km)\n", f.count, f.bestId, f.bestName, f.bestKm);
  if (f.bestKm > MAX_STATION_KM) return Find::None;
  id = f.bestId;
  name = f.bestName;
  return Find::Ok;
}

// ---- Predictions ------------------------------------------------------------

static time_t utcEpoch(int y, int mo, int d, int h, int mi) {  // days-from-civil, no timezone involved
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400, doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + doe - 719468;
  return (time_t)days * 86400 + h * 3600 + mi * 60;
}

enum class Fetch { Ok, BadStation, NetError };

static bool httpsGet(const String &url, String &body) {
  WiFiClientSecure client;
  client.setInsecure();  // public, read-only data (same reasoning as weather.cpp)
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  int status = http.GET();
  if (status == HTTP_CODE_OK) body = http.getString();
  http.end();
  return status == HTTP_CODE_OK;
}

static const char *NOAA = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?format=json&time_zone=gmt&application=esp32_desk_clock";

// "2026-11-12 17:24" (UTC) at `p` -> epoch, or 0.
static time_t parseUtc(const char *p) {
  int Y, M, D, h, m;
  return sscanf(p, "%d-%d-%d %d:%d", &Y, &M, &D, &h, &m) == 5 ? utcEpoch(Y, M, D, h, m) : 0;
}

// High and low tides from yesterday (UTC) for 72 h, which always covers the face's 6 h back / 12 h ahead.
static Fetch fetchTurns(const String &station, char unit, TideData &d) {
  time_t y = time(nullptr) - 86400;
  tm g;
  gmtime_r(&y, &g);
  char day[12];
  strftime(day, sizeof(day), "%Y%m%d", &g);
  String body;
  if (!httpsGet(String(NOAA) + "&product=predictions&interval=hilo&datum=MLLW&range=72&units=" + (unit == 'C' ? "metric" : "english") +
                    "&begin_date=" + day + "&station=" + station, body))
    return Fetch::NetError;
  if (body.indexOf("\"error\"") >= 0) return Fetch::BadStation;
  d = TideData();
  d.unit = unit;
  for (int at = body.indexOf("\"t\":"); at >= 0 && d.n < TideData::MAX; at = body.indexOf("\"t\":", at + 4)) {
    int q = body.indexOf('"', at + 4);
    int v = body.indexOf("\"v\":", at), ty = body.indexOf("\"type\":", at);
    time_t t = q >= 0 ? parseUtc(body.c_str() + q + 1) : 0;
    if (!t || v < 0 || ty < 0) break;
    int vq = body.indexOf('"', v + 4), tq = body.indexOf('"', ty + 7);
    if (vq < 0 || tq < 0) break;
    d.t[d.n] = t;
    d.h[d.n] = atof(body.c_str() + vq + 1);
    d.high[d.n] = body[tq + 1] == 'H';
    d.n++;
  }
  d.valid = d.n >= 2;
  return d.valid ? Fetch::Ok : Fetch::BadStation;
}

// Water temperature: the station's own sensor if it has one and it reported in the last 3 h, else Open-Meteo's
// modelled sea-surface temperature at the ZIP (any coast, ~0.5 degrees off). NAN if neither.
static float fetchWater(const String &station, char unit, float lat, float lon, bool &netError) {
  String body;
  netError = false;
  if (httpsGet(String(NOAA) + "&product=water_temperature&date=latest&units=" + (unit == 'C' ? "metric" : "english") + "&station=" + station, body)) {
    int t = body.indexOf("\"t\":\""), v = body.indexOf("\"v\":\"");
    time_t at = t >= 0 ? parseUtc(body.c_str() + t + 5) : 0;
    bool num = v >= 0 && (isdigit((unsigned char)body[v + 5]) || body[v + 5] == '-');  // "v":"" when the sensor is down
    if (at && num && time(nullptr) - at < 3 * 3600) return atof(body.c_str() + v + 5);
  }
  if (lat == 0 && lon == 0) return NAN;
  body = "";
  if (!httpsGet("https://marine-api.open-meteo.com/v1/marine?current=sea_surface_temperature&temperature_unit=" + String(unit == 'C' ? "celsius" : "fahrenheit") +
                    "&latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4), body)) {
    netError = true;
    return NAN;
  }
  int c = body.indexOf("\"current\":{");
  int k = c >= 0 ? body.indexOf("\"sea_surface_temperature\":", c) : -1;
  if (k < 0) return NAN;
  const char *p = body.c_str() + k + 26;
  return (isdigit((unsigned char)*p) || *p == '-') ? atof(p) : NAN;  // null inland
}

// The display name of a station the user typed in (the nearest one's name comes with the station list).
static String stationName(const String &station) {
  String body;
  if (!httpsGet("https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations/" + station + ".json", body)) return "";
  int k = body.indexOf("\"name\":");
  int q = k >= 0 ? body.indexOf('"', k + 7) : -1, e = q >= 0 ? body.indexOf('"', q + 1) : -1;
  return e > q ? body.substring(q + 1, e) : "";
}

// "CHARLESTON (Customhouse Wharf)" / "SEATTLE (Madison St.), Elliott Bay" -> "CHARLESTON" / "SEATTLE".
static void shortName(const String &full, char *out, size_t n) {
  int cut = full.length();
  for (const char *sep : {" (", ","}) {
    int i = full.indexOf(sep);
    if (i > 0 && i < cut) cut = i;
  }
  strlcpy(out, full.substring(0, cut).c_str(), n);
  for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
}

void tide_refresh(const ClockConfig &cfg0) {
  ClockConfig cfg = cfg0;
  if (!g_mu || WiFi.status() != WL_CONNECTED) return;
  String station = cfg.tideStation, name;
  if (station.length()) {
    static String namedId, namedAs;  // the typed-in station's name, looked up once per ID
    if (namedId != station) {
      namedAs = stationName(station);
      namedId = station;
    }
    name = namedAs;
  } else {
    if (cfg.tideGeo != cfg.city) {  // ZIP changed (or first run): find the nearest station once
      if (cfg.geocity != cfg.city || (cfg.lat == 0 && cfg.lon == 0)) return;  // coordinates not known yet
      String id, nm;
      Find f = findStation(cfg.lat, cfg.lon, id, nm);
      if (f == Find::NetError) return;  // retry on the next refresh
      config_save_tide_auto(cfg.city, id, nm);
      cfg.tideAuto = id;
      cfg.tideName = nm;
    }
    station = cfg.tideAuto;
    name = cfg.tideName;
    if (!station.length()) {
      if (g_for != "none") { g_for = "none"; publishError(cfg.units, "NO TIDE STATION NEARBY"); }
      return;
    }
  }
  String key = station + cfg.units;
  if (!g_at || key != g_for || millis() - g_at >= TIDE_REFRESH_MS) {
    TideData d;
    Fetch r = fetchTurns(station, cfg.units, d);
    if (r == Fetch::NetError) return;  // keep what we have; retry on the next refresh
    bool changed = key != g_for;
    g_at = millis() | 1;
    g_for = key;
    if (r == Fetch::BadStation) {
      publishError(cfg.units, "BAD TIDE STATION");
      return;
    }
    shortName(name.length() ? name : station, d.station, sizeof(d.station));
    if (changed) g_waterAt = 0;  // other station or unit: fetch the water temperature again now
    else d.water = tide_get().water;
    Serial.printf("tide: %d turns from station %s %s\n", d.n, station.c_str(), name.c_str());
    publish(d);
  }
  if (!g_waterAt || millis() - g_waterAt >= WATER_REFRESH_MS) {
    bool netError;
    float w = fetchWater(station, cfg.units, cfg.lat, cfg.lon, netError);
    if (netError) return;
    g_waterAt = millis() | 1;
    xSemaphoreTake(g_mu, portMAX_DELAY);
    g_tide.water = w;
    g_tide.version++;
    xSemaphoreGive(g_mu);
  }
}
