#include "config.h"
#include <Preferences.h>

static const char *NAMESPACE = "clock";
static const char *DEFAULT_TZ = "PST8PDT,M3.2.0,M11.1.0";

// Opens the namespace for the lifetime of the object.
struct Nvs {
  Preferences p;
  Nvs() { p.begin(NAMESPACE, false); }
  ~Nvs() { p.end(); }
};

bool config_load(ClockConfig &cfg) {
  Nvs n;
  Preferences &p = n.p;
  cfg.ssid = p.getString("ssid", "");
  cfg.pass = p.getString("pass", "");
  cfg.tz = p.getString("tz", DEFAULT_TZ);
  cfg.city = p.getString("city", "");
  cfg.units = p.getString("units", "F")[0] == 'C' ? 'C' : 'F';
  cfg.h24 = p.getBool("h24", false);
  cfg.dateFmt = p.getUChar("datefmt", 0) == 1 ? 1 : 0;
  cfg.accent = p.getUChar("accent", 0);
  cfg.face = p.getUChar("face", 0);
  cfg.nightMode = p.getUChar("nmode", 0);
  cfg.nightStart = p.getUChar("nstart", 22) % 24;
  cfg.nightEnd = p.getUChar("nend", 7) % 24;
  cfg.nightColor = p.getUChar("ncolor", 0);
  cfg.chime = p.getBool("chime", true);
  cfg.detail = p.getBool("detail", true);
  cfg.seconds = p.getBool("secs", false);
  cfg.tideStation = p.getString("tidestn", "");
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
  Preferences &p = n.p;
  p.putString("ssid", cfg.ssid);
  p.putString("pass", cfg.pass);
  p.putString("tz", cfg.tz);
  p.putString("city", cfg.city);
  p.putString("units", String(cfg.units));
  p.putBool("h24", cfg.h24);
  p.putUChar("datefmt", cfg.dateFmt);
  p.putUChar("accent", cfg.accent);
  p.putUChar("face", cfg.face);
  p.putUChar("nmode", cfg.nightMode);
  p.putUChar("nstart", cfg.nightStart);
  p.putUChar("nend", cfg.nightEnd);
  p.putUChar("ncolor", cfg.nightColor);
  p.putBool("chime", cfg.chime);
  p.putBool("detail", cfg.detail);
  p.putBool("secs", cfg.seconds);
  p.putString("tidestn", cfg.tideStation);
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
