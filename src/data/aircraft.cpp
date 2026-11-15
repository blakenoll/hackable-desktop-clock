#include "aircraft.h"
#include "../config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/semphr.h>
#include <math.h>

static constexpr uint32_t POLL_MS = 10UL * 1000;      // well under adsb.fi's 1 request/s; the face dead-reckons in between
static constexpr uint32_t BACKOFF_MS = 60UL * 1000;   // after an HTTP error (4xx responses count against the IP)

static AircraftData g_data;
static SemaphoreHandle_t g_mu;
static volatile bool g_active = false;
static TaskHandle_t g_task;

AircraftData aircraft_get() {
  if (!g_mu) return AircraftData();
  xSemaphoreTake(g_mu, portMAX_DELAY);
  AircraftData d = g_data;
  xSemaphoreGive(g_mu);
  return d;
}

static void publish(AircraftData d) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  d.version = g_data.version + 1;
  g_data = d;
  xSemaphoreGive(g_mu);
}

static void publishError(const char *msg) {
  AircraftData d;
  strlcpy(d.error, msg, sizeof(d.error));
  publish(d);
}

// ---- Streaming parser -------------------------------------------------------
// The response is {"msg":..,"now":..,"total":..,"ac":[{...},{...}]}: ~500 bytes per aircraft, so a busy sky is
// tens of KB. It is parsed as it streams in (HTTPClient::writeToStream also undoes chunked encoding) and only the
// MAX nearest aircraft are kept. Aircraft objects sit at nesting depth 3 and are flat apart from a few arrays
// ("mlat":[], "tisb":[]), which are skipped.
class AcParser : public Stream {
 public:
  AircraftData out;

  size_t write(uint8_t c) override {
    if (inStr) {
      if (esc) { esc = false; add(c); }
      else if (c == '\\') esc = true;
      else if (c == '"') endString();
      else add(c);
      return 1;
    }
    switch (c) {
      case '"':
        inStr = true;
        len = 0;
        break;
      case '{': case '[':
        if (depth == 2 && c == '{') cur = Cur();  // a new aircraft
        depth++;
        break;
      case '}': case ']':
        if (depth == 3) scalar();
        if (depth == 3 && c == '}') commit();
        depth--;
        if (depth == 3) afterColon = false;  // a nested value just ended
        break;
      case ':':
        if (depth == 3) { afterColon = true; len = 0; }
        break;
      case ',':
        if (depth == 3) scalar();
        break;
      case ' ': case '\n': case '\r': case '\t':
        break;
      default:
        if (depth == 3 && afterColon) add(c);
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
  struct Cur {
    float dst = NAN, dir = NAN, gs = 0, track = NAN, seen = 0;
    int alt = -1, rate = 0;
    char flight[9] = "", reg[9] = "", hex[9] = "", type[5] = "";
  } cur;
  int depth = 0;
  bool inStr = false, esc = false, afterColon = false;
  char key[12] = "", val[24];
  int len = 0;

  void add(uint8_t c) { if (len < (int)sizeof(val) - 1) val[len++] = (char)c; }

  void endString() {
    inStr = false;
    val[len] = 0;
    if (depth != 3) return;
    if (!afterColon) { strlcpy(key, val, sizeof(key)); return; }
    value(true);
    afterColon = false;
    len = 0;
  }
  void scalar() {  // a number / true / null after the colon
    if (!afterColon || !len) { afterColon = false; return; }
    val[len] = 0;
    value(false);
    afterColon = false;
    len = 0;
  }
  static void trimCopy(char *dst, size_t n, const char *s) {
    strlcpy(dst, s, n);
    for (int i = (int)strlen(dst) - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
  }
  void value(bool str) {
    const char *k = key;
    if (!strcmp(k, "flight") && str) trimCopy(cur.flight, sizeof(cur.flight), val);
    else if (!strcmp(k, "r") && str) trimCopy(cur.reg, sizeof(cur.reg), val);
    else if (!strcmp(k, "hex") && str) trimCopy(cur.hex, sizeof(cur.hex), val);
    else if (!strcmp(k, "t") && str) trimCopy(cur.type, sizeof(cur.type), val);
    else if (!strcmp(k, "alt_baro")) cur.alt = str ? 0 : (int)lroundf(atof(val));  // the string is "ground"
    else if (str) return;
    else if (!strcmp(k, "dst")) cur.dst = atof(val);
    else if (!strcmp(k, "dir")) cur.dir = atof(val);
    else if (!strcmp(k, "gs")) cur.gs = atof(val);
    else if (!strcmp(k, "track")) cur.track = atof(val);
    else if (!strcmp(k, "baro_rate")) cur.rate = atoi(val);
    else if (!strcmp(k, "seen_pos")) cur.seen = atof(val);
  }
  void commit() {
    // Aircraft on the ground (taxiing at the airport) would only crowd the middle: the scope shows the sky.
    if (isnan(cur.dst) || isnan(cur.dir) || cur.dst > RADAR_NM || cur.alt == 0) return;
    out.total++;
    float b = cur.dir * (float)M_PI / 180, x = cur.dst * sinf(b), y = cur.dst * cosf(b);
    float track = isnan(cur.track) ? 0 : cur.track;
    if (!isnan(cur.track) && cur.seen > 0 && cur.seen < 60) {  // bring an older position up to "now"
      float t = track * (float)M_PI / 180, d = cur.gs * cur.seen / 3600;
      x += d * sinf(t);
      y += d * cosf(t);
    }
    // Keep the MAX nearest, sorted: find the slot, shift the rest down (the farthest falls off).
    float dist = hypotf(x, y);
    int at = out.n;
    while (at > 0 && hypotf(out.ac[at - 1].x, out.ac[at - 1].y) > dist) at--;
    if (at >= AircraftData::MAX) return;
    int last = out.n < AircraftData::MAX ? out.n : AircraftData::MAX - 1;
    for (int i = last; i > at; i--) out.ac[i] = out.ac[i - 1];
    if (out.n < AircraftData::MAX) out.n++;
    Aircraft &a = out.ac[at];
    a = Aircraft();
    a.x = x;
    a.y = y;
    a.gs = cur.gs;
    a.track = track;
    a.alt = cur.alt;
    a.rate = cur.rate;
    strlcpy(a.flight, cur.flight[0] ? cur.flight : cur.reg[0] ? cur.reg : cur.hex, sizeof(a.flight));
    for (char *p = a.flight; *p; p++) *p = (char)toupper((unsigned char)*p);  // the hex code comes lowercase
    strlcpy(a.type, cur.type, sizeof(a.type));
  }
};

// Returns the HTTP status (or a negative HTTPClient error); fills `d` on 200.
static int fetch(float lat, float lon, AircraftData &d) {
  WiFiClientSecure client;
  client.setInsecure();  // public, read-only data (same reasoning as weather.cpp)
  HTTPClient http;
  http.setTimeout(8000);
  char url[112];
  snprintf(url, sizeof(url), "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d", lat, lon, RADAR_NM);
  if (!http.begin(client, url)) return -1;
  int status = http.GET();
  if (status == HTTP_CODE_OK) {
    AcParser p;
    uint32_t t0 = millis();
    int got = http.writeToStream(&p);
    int size = http.getSize();
    if (got <= 0 || (size > 0 && got < size)) status = -2;  // cut off: a partial sky would look like planes vanishing
    else {
      d = p.out;
      d.valid = true;
      d.fetchedAt = t0;
    }
  }
  http.end();
  return status;
}

static void radarTask(void *) {
  for (;;) {
    if (!g_active || WiFi.status() != WL_CONNECTED) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    ClockConfig cfg;
    config_load(cfg);
    if (!cfg.city.length() || cfg.geocity != cfg.city || (cfg.lat == 0 && cfg.lon == 0)) {
      // No ZIP, or the weather task has not geocoded it yet.
      const char *msg = cfg.city.length() ? "LOCATING" : "SET A ZIP FOR RADAR";
      if (strcmp(aircraft_get().error, msg) != 0) publishError(msg);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    AircraftData d;
    int status = fetch(cfg.lat, cfg.lon, d);
    uint32_t wait = POLL_MS;
    if (status == HTTP_CODE_OK) {
      publish(d);
    } else {
      Serial.printf("radar: fetch failed (%d)\n", status);
      AircraftData old = aircraft_get();
      if (!old.valid) publishError("NO RADAR DATA");  // keep showing (dead-reckoned) planes for a while otherwise
      if (status >= 400) wait = BACKOFF_MS;
    }
    // Woken early by aircraft_set_active(true) only after a pause, so switching faces can't beat the rate limit.
    vTaskDelay(pdMS_TO_TICKS(wait));
  }
}

void aircraft_set_active(bool on) {
  if (on == g_active) return;
  g_active = on;
  if (!on) return;
  if (!g_mu) {
    g_mu = xSemaphoreCreateMutex();
    xTaskCreate(radarTask, "radar", 10240, nullptr, 1, &g_task);
  }
  AircraftData d = aircraft_get();
  if (!d.valid || millis() - d.fetchedAt > 3 * POLL_MS) {  // old planes would be wrong: scan again
    AircraftData l;
    l.loading = true;
    publish(l);
  }
  xTaskNotifyGive(g_task);
}
