// NVS (flash) persistence for ClockConfig. The user settings are listed once in config_fields.h.
#include "config.h"
#include <Preferences.h>
#include "config_fields.h"

static const char *NAMESPACE = "clock";

// Opens the namespace for the lifetime of the object.
struct Nvs {
  Preferences p;
  Nvs() { p.begin(NAMESPACE, false); }
  ~Nvs() { p.end(); }
};

// Visitors for config_fields() (config_fields.h).
struct Loader {
  Preferences &p;
  void text(const char *k, String &f, const char *def) { f = p.getString(k, def); }
  void secret(const char *k, String &f) { f = p.getString(k, ""); }
  void unit(const char *k, char &f) { f = p.getString(k, "F")[0] == 'C' ? 'C' : 'F'; }
  void flag(const char *k, bool &f, bool def) { f = p.getBool(k, def); }
  void number(const char *k, uint8_t &f, uint8_t def, int lo, int hi) { f = (uint8_t)constrain((int)p.getUChar(k, def), lo, hi); }
};

struct Saver {
  Preferences &p;
  void text(const char *k, String &f, const char *) { p.putString(k, f); }
  void secret(const char *k, String &f) { p.putString(k, f); }
  void unit(const char *k, char &f) { p.putString(k, String(f)); }
  void flag(const char *k, bool &f, bool) { p.putBool(k, f); }
  void number(const char *k, uint8_t &f, uint8_t, int, int) { p.putUChar(k, f); }
};

bool config_load(ClockConfig &cfg) {
  Nvs n;
  Preferences &p = n.p;
  Loader l{p};
  config_fields(l, cfg);
  // Caches written by the background tasks, not the setup page.
  cfg.tideGeo = p.getString("tidegeo", "");
  cfg.tideAuto = p.getString("tideauto", "");
  cfg.tideName = p.getString("tidename", "");
  cfg.geocity = p.getString("geocity", "");
  cfg.lat = p.getFloat("lat", 0);
  cfg.lon = p.getFloat("lon", 0);
  return cfg.ssid.length() > 0;
}

void config_save(const ClockConfig &cfg) {
  Nvs n;
  Saver s{n.p};
  ClockConfig c = cfg;  // config_fields() takes a mutable config; Saver only reads it
  config_fields(s, c);
}

String config_geo_key(const ClockConfig &cfg) {
  return cfg.addr.length() ? cfg.city + "|" + cfg.addr : cfg.city;
}

void config_save_geo(const String &city, float lat, float lon) {
  Nvs n;
  n.p.putString("geocity", city);
  n.p.putFloat("lat", lat);
  n.p.putFloat("lon", lon);
}

void config_save_tide_auto(const String &city, const String &id, const String &name) {
  Nvs n;
  n.p.putString("tidegeo", city);
  n.p.putString("tideauto", id);
  n.p.putString("tidename", name);
}

bool config_load_night() {
  Nvs n;
  return n.p.getBool("night", false);
}

void config_save_night(bool night) {
  Nvs n;
  n.p.putBool("night", night);
}

void config_save_face(uint8_t face) {
  Nvs n;
  n.p.putUChar("face", face);
}

void config_save_theme(uint8_t theme) {
  Nvs n;
  n.p.putUChar("accent", theme);
}
