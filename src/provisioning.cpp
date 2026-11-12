#include "provisioning.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <lvgl.h>
#include "config.h"
#include "ui.h"

static constexpr uint32_t IDLE_REBOOT_MS = 5UL * 60 * 1000;
static const char *AP_SSID_PREFIX = "clk-setup";  // a per-board suffix is appended at runtime

struct Zone {
  const char *label, *posix;
};
static const Zone ZONES[] = {
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Arizona (no DST)", "MST7"},
    {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US Hawaii", "HST10"},
    {"Newfoundland", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"Brazil (Sao Paulo)", "<-03>3"},
    {"UTC", "UTC0"},
    {"UK / Ireland", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"Dubai", "<+04>-4"},
    {"India", "IST-5:30"},
    {"Singapore / Perth", "<+08>-8"},
    {"China / Hong Kong", "CST-8"},
    {"Japan / Korea", "JST-9"},
    {"Sydney / Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

// ---- Web page ---------------------------------------------------------------

static String esc(const String &s) {
  String o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

static const char PAGE_HEAD[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Desk Clock Setup</title>
<style>body{font:16px system-ui,sans-serif;margin:auto;padding:20px;max-width:420px;background:#111;color:#eee}
h1{font-size:20px}label{display:block;margin:16px 0 4px;color:#aaa}
input,select,button{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border-radius:6px;border:1px solid #444;background:#222;color:#eee}
button{background:#0a84ff;border:0;margin-top:24px}small{color:#888}</style></head><body>)HTML";

// <option> for a select whose current value is `selected`.
static String option(int value, int selected, const char *label) {
  return String("<option value=") + value + (value == selected ? " selected" : "") + ">" + label + "</option>";
}

// A labelled <select> with the given <option>s.
static String field(const char *label, const char *name, const String &options, const char *hint = nullptr) {
  String h = String("<label>") + label;
  if (hint) h += String(" <small>(") + hint + ")</small>";
  return h + "</label><select name=" + name + ">" + options + "</select>";
}

static String onOff(int on, int off, int cur, const char *onLabel, const char *offLabel) {
  return option(on, cur, onLabel) + option(off, cur, offLabel);
}

// WiFi: a dropdown of scanned networks plus "Other..." which reveals a manual text field. The text input
// (name=ssid) is what gets submitted; the select just fills it. Without JS the field stays visible.
static String wifiFields(const String &netOptions, const ClockConfig &cur) {
  bool known = netOptions.indexOf("value=\"" + esc(cur.ssid) + "\"") >= 0 && cur.ssid.length() > 0;
  String opts = netOptions;
  if (known) opts.replace("value=\"" + esc(cur.ssid) + "\"", "value=\"" + esc(cur.ssid) + "\" selected");
  return "<label>WiFi network (2.4 GHz)</label>"
         "<select id=sel><option value=\"\">Choose a network...</option>" + opts +
         "<option value=\"__other\"" + (!known && cur.ssid.length() ? " selected" : "") + ">Other (type manually)...</option></select>"
         "<input id=ssid name=ssid required autocapitalize=off autocorrect=off placeholder=\"Network name\" value=\"" + esc(cur.ssid) + "\">"
         "<script>var s=document.getElementById('sel'),i=document.getElementById('ssid');"
         "function u(){var o=s.value==='__other'||s.value==='';i.style.display=o?'block':'none';"
         "if(s.value&&!o)i.value=s.value;else if(s.value==='__other'&&i.value&&[].some.call(s.options,function(x){return x.value===i.value}))i.value='';}"
         "s.onchange=function(){u();if(s.value==='__other')i.focus()};"
         "if(s.value&&s.value!=='__other')i.value=s.value;u();</script>"
         "<label>WiFi password</label>"
         "<input name=pass type=text autocapitalize=off autocorrect=off value=\"" + esc(cur.pass) + "\">";
}

static String timezoneField(const ClockConfig &cur) {
  String opts;
  bool matched = false;
  for (const Zone &z : ZONES) {
    bool sel = cur.tz == z.posix;
    matched |= sel;
    opts += String("<option value=\"") + esc(z.posix) + "\"" + (sel ? " selected" : "") + ">" + z.label + "</option>";
  }
  // A previously saved zone that isn't in the presets stays selectable instead of silently resetting.
  if (!matched && cur.tz.length()) opts += "<option value=\"" + esc(cur.tz) + "\" selected>Saved: " + esc(cur.tz) + "</option>";
  return field("Timezone", "tz", opts);
}

static String hoursField(const char *label, const char *name, int sel, bool h24) {
  String opts;
  for (int hr = 0; hr < 24; hr++) {
    char text[12];
    if (h24) snprintf(text, sizeof(text), "%02d:00", hr);
    else snprintf(text, sizeof(text), "%d %s", hr % 12 == 0 ? 12 : hr % 12, hr < 12 ? "AM" : "PM");
    opts += option(hr, sel, text);
  }
  return field(label, name, opts);
}

static String displayFields(const ClockConfig &cur) {
  String faces, themes;
  for (int i = 0; i < ui_face_count(); i++) faces += option(i, cur.face, ui_face_name(i));
  for (int i = 0; i < ui_theme_count(); i++) themes += option(i, cur.accent, ui_theme_name(i));
  return field("Time format", "h24", onOff(0, 1, cur.h24, "12-hour (2:30 PM)", "24-hour (14:30)")) +
         field("Date format", "datefmt", onOff(0, 1, cur.dateFmt, "Fri Nov 6", "Fri 6 Nov")) +
         field("Clock face", "face", faces, "a short press of the BOOT button also cycles these") +
         field("Colour theme", "accent", themes, "a double press of BOOT cycles these") +
         field("Seconds as digits", "secs", onOff(0, 1, cur.seconds, "Off", "On")) +
         field("Hourly chime", "chime", onOff(1, 0, cur.chime, "On", "Off"), "comet lap on the hour") +
         field("Weather detail line", "detail", onOff(1, 0, cur.detail, "Show", "Hide"), "rain chance / feels like");
}

static String nightFields(const ClockConfig &cur) {
  return field("Night mode", "nmode",
               option(0, cur.nightMode, "After sunset (needs ZIP)") + option(1, cur.nightMode, "Fixed hours") + option(2, cur.nightMode, "Never")) +
         hoursField("Fixed hours: night starts", "nstart", cur.nightStart, cur.h24) +
         hoursField("Fixed hours: night ends", "nend", cur.nightEnd, cur.h24) +
         field("Night colour", "ncolor", option(0, cur.nightColor, "Red") + option(1, cur.nightColor, "Amber") + option(2, cur.nightColor, "Dim white"));
}

static String weatherFields(const ClockConfig &cur) {
  return "<label>ZIP code for weather <small>(optional, US 5-digit)</small></label>"
         "<input name=city inputmode=numeric pattern=\"[0-9]{5}\" maxlength=5 title=\"5-digit US ZIP code\" value=\"" + esc(cur.city) + "\">" +
         field("Temperature units", "units", String("<option value=F") + (cur.units == 'F' ? " selected" : "") + ">Fahrenheit</option><option value=C" +
                                                 (cur.units == 'C' ? " selected" : "") + ">Celsius</option>") +
         "<label>NOAA tide station <small>(optional, station ID for the Tide face, e.g. 8665530; blank = nearest" +
         (cur.tideGeo == cur.city && cur.tideAuto.length() ? ", now " + esc(cur.tideAuto) + " " + esc(cur.tideName) : String("")) +
         ")</small></label><input name=tidestn pattern=\"[A-Za-z0-9]{4,10}\" maxlength=10 title=\"NOAA station ID from tidesandcurrents.noaa.gov\" value=\"" +
         esc(cur.tideStation) + "\">";
}

static String buildForm(const String &netOptions, const ClockConfig &cur) {
  return String(PAGE_HEAD) + "<h1>Desk Clock Setup</h1><form method=post action=/save>" + wifiFields(netOptions, cur) +
         timezoneField(cur) + displayFields(cur) + nightFields(cur) + weatherFields(cur) +
         "<button type=submit>Save &amp; restart</button></form></body></html>";
}

// Async scan so the sonar animation keeps running (lv_timer_handler) while it works.
static String scanOptions() {
  WiFi.scanNetworks(true);
  uint32_t t0 = millis();
  int n;
  while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - t0 < 15000) {
    lv_timer_handler();
    delay(20);
  }
  if (n < 0) n = 0;  // failed or timed out: offer manual entry only
  String opts;
  bool used[64] = {false};
  if (n > 64) n = 64;
  // Strongest first; skip hidden and duplicate names (keeps the strongest of each).
  for (int pass = 0; pass < n; pass++) {
    int best = -1;
    for (int i = 0; i < n; i++)
      if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    if (best < 0) break;
    used[best] = true;
    String ssid = WiFi.SSID(best);
    if (ssid.length() == 0 || opts.indexOf("value=\"" + esc(ssid) + "\"") >= 0) continue;
    bool open = WiFi.encryptionType(best) == WIFI_AUTH_OPEN;
    opts += "<option value=\"" + esc(ssid) + "\">" + esc(ssid) + (open ? "" : " \xF0\x9F\x94\x92") + "</option>";
  }
  WiFi.scanDelete();
  return opts;
}

// Reads the submitted form into `n`. Out-of-range numbers are clamped.
static void parseForm(WebServer &server, ClockConfig &n) {
  auto num = [&](const char *name, int lo, int hi) { return (uint8_t)constrain(server.arg(name).toInt(), lo, hi); };
  n.ssid = server.arg("ssid");
  n.ssid.trim();
  n.pass = server.arg("pass");
  n.tz = server.arg("tz");
  n.city = server.arg("city");
  n.city.trim();
  n.units = server.arg("units") == "C" ? 'C' : 'F';
  n.tideStation = server.arg("tidestn");
  n.tideStation.trim();
  n.h24 = server.arg("h24") == "1";
  n.dateFmt = server.arg("datefmt") == "1" ? 1 : 0;
  n.accent = num("accent", 0, ui_theme_count() - 1);
  n.face = num("face", 0, ui_face_count() - 1);
  n.nightMode = num("nmode", 0, 2);
  n.nightStart = num("nstart", 0, 23);
  n.nightEnd = num("nend", 0, 23);
  n.nightColor = num("ncolor", 0, 2);
  n.chime = server.arg("chime") != "0";
  n.detail = server.arg("detail") != "0";
  n.seconds = server.arg("secs") == "1";
}

// ---- Setup mode -------------------------------------------------------------

[[noreturn]] void prov_run(bool hasSaved) {
  ClockConfig cur;
  config_load(cur);

  // Scan before the AP goes up: scanning while serving would hop channels and
  // drop the client's connection.
  ui_set_scanning(true);
  lv_timer_handler();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  String netOptions = scanOptions();
  ui_set_scanning(false);

  char apPass[9];
  snprintf(apPass, sizeof(apPass), "%08lu", (unsigned long)(esp_random() % 100000000UL));
  WiFi.mode(WIFI_AP);
  // "clk-setup-A1B2": last two bytes of this chip's MAC, so several clocks being set up
  // at once each advertise a distinct, stable name.
  char apSsid[32];
  uint64_t efuse = ESP.getEfuseMac();  // byte 0 of the MAC is the lowest byte
  snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", AP_SSID_PREFIX, (unsigned)((efuse >> 32) & 0xFF),
           (unsigned)((efuse >> 40) & 0xFF));
  WiFi.softAP(apSsid, apPass);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Setup AP \"%s\" password %s at %s\n", apSsid, apPass, ip.toString().c_str());

  ui_show_setup(apSsid, apPass, ip.toString().c_str());

  DNSServer dns;
  dns.start(53, "*", ip);  // answer every hostname with our IP -> captive portal

  WebServer server(80);
  String page = buildForm(netOptions, cur);
  server.on("/", HTTP_GET, [&]() { server.send(200, "text/html", page); });
  server.on("/save", HTTP_POST, [&]() {
    ClockConfig n;
    parseForm(server, n);
    if (n.ssid.length() == 0) {
      server.send(400, "text/plain", "WiFi network name is required.");
      return;
    }
    config_save(n);
    server.send(200, "text/html",
                String(PAGE_HEAD) + "<h1>Saved</h1><p>The clock is restarting and will join <b>" + esc(n.ssid) +
                    "</b>. You can close this page.</p></body></html>");
    delay(1500);
    ESP.restart();
  });
  server.onNotFound([&]() {  // OS captive-portal probes land here
    server.sendHeader("Location", String("http://") + ip.toString() + "/");
    server.send(302, "text/plain", "");
  });
  server.begin();

  uint32_t lastActive = millis();
  while (true) {
    dns.processNextRequest();
    server.handleClient();
    lv_timer_handler();
    if (WiFi.softAPgetStationNum() > 0) lastActive = millis();
    if (hasSaved && millis() - lastActive > IDLE_REBOOT_MS) ESP.restart();  // retry saved WiFi
    delay(5);
  }
}
