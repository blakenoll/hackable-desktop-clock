// Settings API (see api.h, and docs/API.md for the endpoints): JSON over plain HTTP, built on the same
// config_fields() list as NVS and the setup page, so every setting is covered with the same keys, defaults
// and ranges. Requests are answered from loop() (api_poll) or the setup portal's loop, never from a task.
#include "api.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "../config_fields.h"
#include "../setup/zones.h"
#include "../tuning.h"
#include "../ui/ui.h"

static constexpr int API_VERSION = 1;              // bump on incompatible changes; GET /api/info reports it
static constexpr const char *MDNS_SERVICE = "deskclock";  // advertised as _deskclock._tcp
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"  // set by git_version.py
#endif

static WebServer *srv = nullptr;
static ClockConfig *cur = nullptr;
static bool setupMode = false;
static void (*onApplied)(const ClockConfig &prev) = nullptr;

// Settings the running clock only reads at boot: changing one saves and restarts.
static const char *const RESTART_KEYS[] = {"ssid", "pass", "city", "addr", "units", "tidestn"};

static bool needsRestart(const char *k) {
  for (const char *r : RESTART_KEYS)
    if (!strcmp(k, r)) return true;
  return false;
}

// ---- Visitors for config_fields() --------------------------------------------------------------------

// Writes the settings as JSON. The WiFi password is never sent.
struct JsonWriter {
  JsonObject out;
  void text(const char *k, String &f, const char *) { out[k] = f; }
  void secret(const char *, String &) {}
  void unit(const char *k, char &f) { out[k] = String(f); }
  void flag(const char *k, bool &f, bool) { out[k] = f; }
  void number(const char *k, uint8_t &f, uint8_t, int, int) { out[k] = f; }
};

// Server-side checks for text settings (the setup page's HTML patterns only check in the browser).
static const char *textProblem(const char *k, const String &s) {
  if (!strcmp(k, "ssid")) return s.length() < 1 || s.length() > 32 ? "must be 1 to 32 characters" : nullptr;
  if (!strcmp(k, "tz")) return s.length() < 1 || s.length() > 63 ? "must be a POSIX TZ string, e.g. PST8PDT,M3.2.0,M11.1.0" : nullptr;
  if (!strcmp(k, "addr")) return s.length() > 100 ? "must be at most 100 characters" : nullptr;
  if (!strcmp(k, "city")) {
    bool ok = s.length() == 0 || s.length() == 5;
    for (char c : s) ok &= isdigit((unsigned char)c) != 0;
    return ok ? nullptr : "must be a 5-digit US ZIP code, or \"\" to turn weather off";
  }
  if (!strcmp(k, "tidestn")) {
    bool ok = s.length() == 0 || (s.length() >= 4 && s.length() <= 10);
    for (char c : s) ok &= isalnum((unsigned char)c) != 0;
    return ok ? nullptr : "must be a NOAA station ID (4 to 10 letters or digits), or \"\" for the nearest";
  }
  return nullptr;
}

// Applies the keys present in a JSON object; absent keys keep their current value. Records the first problem
// instead of clamping, so a client learns about a bad value instead of having it silently changed.
struct JsonReader {
  JsonObjectConst in;
  String error;
  bool restart = false;  // a RESTART_KEYS setting changed
  explicit JsonReader(JsonObjectConst o) : in(o) {}

  void bad(const char *k, const char *why) {
    if (!error.length()) error = String(k) + ": " + why;
  }
  template <class T>
  void set(const char *k, T &f, const T &v) {
    if (f == v) return;
    f = v;
    restart |= needsRestart(k);
  }
  void text(const char *k, String &f, const char *) {
    JsonVariantConst v = in[k];
    if (v.isNull()) return;
    if (!v.is<const char *>()) return bad(k, "must be a string");
    String s = v.as<const char *>();
    s.trim();
    if (const char *why = textProblem(k, s)) return bad(k, why);
    set(k, f, s);
  }
  void secret(const char *k, String &f) {
    JsonVariantConst v = in[k];
    if (v.isNull()) return;
    if (!v.is<const char *>()) return bad(k, "must be a string");
    String s = v.as<const char *>();  // never trimmed: spaces can be part of a password
    if (s.length() > 63 || (s.length() > 0 && s.length() < 8)) return bad(k, "must be 8 to 63 characters, or \"\" for an open network");
    set(k, f, s);
  }
  void unit(const char *k, char &f) {
    JsonVariantConst v = in[k];
    if (v.isNull()) return;
    String s = v.is<const char *>() ? v.as<const char *>() : "";
    if (s != "F" && s != "C") return bad(k, "must be \"F\" or \"C\"");
    set(k, f, s[0]);
  }
  void flag(const char *k, bool &f, bool) {
    JsonVariantConst v = in[k];
    if (v.isNull()) return;
    if (!v.is<bool>()) return bad(k, "must be true or false");
    set(k, f, v.as<bool>());
  }
  void number(const char *k, uint8_t &f, uint8_t, int lo, int hi) {
    JsonVariantConst v = in[k];
    if (v.isNull()) return;
    if (!v.is<int>() || v.as<int>() < lo || v.as<int>() > hi) {
      String why = String("must be a whole number from ") + lo + " to " + hi;
      return bad(k, why.c_str());
    }
    set(k, f, (uint8_t)v.as<int>());
  }
};

// Finds out whether `want` is a setting at all.
struct KeyFinder {
  const char *want;
  bool found = false;
  explicit KeyFinder(const char *k) : want(k) {}
  void hit(const char *k) { found |= !strcmp(k, want); }
  void text(const char *k, String &, const char *) { hit(k); }
  void secret(const char *k, String &) { hit(k); }
  void unit(const char *k, char &) { hit(k); }
  void flag(const char *k, bool &, bool) { hit(k); }
  void number(const char *k, uint8_t &, uint8_t, int, int) { hit(k); }
};

static void choice(JsonArray a, int value, const char *label) {
  JsonObject o = a.add<JsonObject>();
  o["value"] = value;
  o["label"] = label;
}
static void choice(JsonArray a, const char *value, const char *label) {
  JsonObject o = a.add<JsonObject>();
  o["value"] = value;
  o["label"] = label;
}

// Describes every setting: type, default, range and, where there's a fixed list, the choices with labels.
// Faces, themes, night colours and timezones come from the firmware's own tables, so a client built against
// the schema keeps working when someone adds a face or a theme.
struct SchemaWriter {
  JsonArray out;
  JsonObject add(const char *k, const char *type) {
    JsonObject o = out.add<JsonObject>();
    o["key"] = k;
    o["type"] = type;
    o["restart"] = needsRestart(k);
    return o;
  }
  void text(const char *k, String &, const char *def) {
    JsonObject o = add(k, "text");
    o["default"] = def;
    if (!strcmp(k, "tz")) {  // presets; any POSIX TZ string is accepted
      JsonArray a = o["choices"].to<JsonArray>();
      for (int i = 0; i < ZONE_COUNT; i++) choice(a, ZONES[i].posix, ZONES[i].label);
    }
  }
  void secret(const char *k, String &) { add(k, "secret"); }  // write-only
  void unit(const char *k, char &) {
    JsonObject o = add(k, "choice");
    o["default"] = "F";
    JsonArray a = o["choices"].to<JsonArray>();
    choice(a, "F", "Fahrenheit");
    choice(a, "C", "Celsius");
  }
  void flag(const char *k, bool &, bool def) { add(k, "bool")["default"] = def; }
  void number(const char *k, uint8_t &, uint8_t def, int lo, int hi) {
    JsonObject o = add(k, "number");
    o["default"] = def;
    o["min"] = lo;
    o["max"] = hi;
    JsonArray a;
    if (!strcmp(k, "face")) {
      a = o["choices"].to<JsonArray>();
      for (int i = 0; i < ui_face_count(); i++) choice(a, i, ui_face_name(i));
    } else if (!strcmp(k, "accent")) {
      a = o["choices"].to<JsonArray>();
      for (int i = 0; i < ui_theme_count(); i++) choice(a, i, ui_theme_name(i));
    } else if (!strcmp(k, "ncolor")) {
      a = o["choices"].to<JsonArray>();
      for (int i = 0; i < ui_night_color_count(); i++) choice(a, i, ui_night_color_name(i));
    } else if (!strcmp(k, "datefmt")) {
      a = o["choices"].to<JsonArray>();
      choice(a, 0, "Fri Nov 6");
      choice(a, 1, "Fri 6 Nov");
    } else if (!strcmp(k, "nmode")) {
      a = o["choices"].to<JsonArray>();
      choice(a, 0, "After sunset (needs ZIP)");
      choice(a, 1, "Fixed hours");
      choice(a, 2, "Never");
    }
  }
};

// ---- HTTP --------------------------------------------------------------------------------------------

static void reply(int code, const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  srv->send(code, "application/json", body);
}

static void fail(int code, const String &msg) {
  JsonDocument d;
  d["error"] = msg;
  reply(code, d);
}

// Same length and every byte equal, without stopping at the first difference.
static bool sameSecret(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

// Normal mode needs `Authorization: Bearer <token>`. Setup mode is already behind the access point's password.
static bool authorized() {
  if (setupMode) return true;
  String h = srv->header("Authorization");
  if (h.startsWith("Bearer ") && sameSecret(h.substring(7), config_api_token())) return true;
  srv->sendHeader("WWW-Authenticate", "Bearer");
  fail(401, "missing or wrong API key (Authorization: Bearer <key>); the setup page shows the key");
  return false;
}

static void handleInfo() {
  JsonDocument d;
  d["api"] = API_VERSION;
  d["id"] = api_device_id();
  d["version"] = FIRMWARE_VERSION;
  d["host"] = api_hostname();
  d["mode"] = setupMode ? "setup" : "normal";
  d["auth"] = !setupMode;
  reply(200, d);
}

static void handleSchema() {
  JsonDocument d;
  d["api"] = API_VERSION;
  SchemaWriter w{d["fields"].to<JsonArray>()};
  ClockConfig scratch;
  config_fields(w, scratch);
  reply(200, d);
}

static void writeConfig(JsonObject out) {
  JsonWriter w{out};
  config_fields(w, *cur);
}

static void handleGet() {
  if (!authorized()) return;
  JsonDocument d;
  writeConfig(d.to<JsonObject>());
  if (setupMode) d["token"] = config_api_token();  // so an app doing first-time setup can keep talking afterwards
  reply(200, d);
}

static void handlePost() {
  if (!authorized()) return;
  String body = srv->arg("plain");
  JsonDocument in;
  if (!body.length() || deserializeJson(in, body) || !in.is<JsonObject>())
    return fail(400, "send the settings to change as a JSON object (Content-Type: application/json)");
  JsonObjectConst obj = in.as<JsonObjectConst>();
  for (JsonPairConst kv : obj) {
    KeyFinder f{kv.key().c_str()};
    ClockConfig scratch;
    config_fields(f, scratch);
    if (!f.found) return fail(400, String(kv.key().c_str()) + ": unknown setting (GET /api/schema lists them)");
  }

  ClockConfig next = *cur;
  JsonReader r{obj};
  config_fields(r, next);
  if (r.error.length()) return fail(400, r.error);
  if (!next.ssid.length()) return fail(400, "ssid: required");

  bool restart = setupMode || r.restart;
  config_save(next);
  ClockConfig prev = *cur;
  *cur = next;

  JsonDocument d;
  d["ok"] = true;
  d["restart"] = restart;
  writeConfig(d["config"].to<JsonObject>());
  reply(200, d);
  if (restart) {
    delay(1000);  // let the response reach the client
    ESP.restart();
  }
  if (onApplied) onApplied(prev);
}

static void routes(WebServer &s) {
  s.on("/api/info", HTTP_GET, handleInfo);
  s.on("/api/schema", HTTP_GET, handleSchema);
  s.on("/api/config", HTTP_GET, handleGet);
  s.on("/api/config", HTTP_POST, handlePost);
}

// ---- Public ------------------------------------------------------------------------------------------

String api_device_suffix() {
  uint64_t efuse = ESP.getEfuseMac();  // byte 0 of the MAC is the lowest byte
  char s[5];
  snprintf(s, sizeof(s), "%02X%02X", (unsigned)((efuse >> 32) & 0xFF), (unsigned)((efuse >> 40) & 0xFF));
  return s;
}

String api_device_id() {
  uint64_t efuse = ESP.getEfuseMac();
  char s[13];
  for (int i = 0; i < 6; i++) snprintf(s + 2 * i, 3, "%02x", (unsigned)((efuse >> (8 * i)) & 0xFF));
  return s;
}

String api_hostname() {
  String h = String(API_HOSTNAME_PREFIX) + "-" + api_device_suffix();
  h.toLowerCase();
  return h;
}

void api_begin(ClockConfig &cfg, void (*applied)(const ClockConfig &prev)) {
  cur = &cfg;
  onApplied = applied;
  setupMode = false;
  config_api_token();  // create it now (the radio is on, so esp_random() is truly random)
  srv = new WebServer(API_PORT);
  const char *headers[] = {"Authorization"};
  srv->collectHeaders(headers, 1);
  routes(*srv);
  srv->onNotFound([] { fail(404, "no such endpoint (try /api/info)"); });
  srv->begin();

  String host = api_hostname();
  if (MDNS.begin(host.c_str())) {
    MDNS.addService(MDNS_SERVICE, "tcp", API_PORT);
    MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "api", String(API_VERSION));
    MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "id", api_device_id());
    MDNS.addService("http", "tcp", API_PORT);
  }
  Serial.printf("Settings API at http://%s.local/api/info (%s)\n", host.c_str(), WiFi.localIP().toString().c_str());
}

void api_poll() {
  if (srv) srv->handleClient();
}

void api_setup_routes(WebServer &server, ClockConfig &cfg) {
  srv = &server;
  cur = &cfg;
  onApplied = nullptr;
  setupMode = true;
  routes(server);
}
