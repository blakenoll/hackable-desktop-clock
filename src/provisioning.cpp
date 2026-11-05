#include "provisioning.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <lvgl.h>
#include "ui.h"

static constexpr int BOOT_PIN = 9;
static constexpr uint32_t LONG_PRESS_MS = 3000;
static constexpr uint32_t IDLE_REBOOT_MS = 5UL * 60 * 1000;
static const char *AP_SSID = "DeskClock-Setup";
static const char *DEFAULT_TZ = "PST8PDT,M3.2.0,M11.1.0";

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

// ---- Persistence ------------------------------------------------------------

bool prov_load(ClockConfig &cfg) {
  Preferences p;
  p.begin("clock", false);
  cfg.ssid = p.getString("ssid", "");
  cfg.pass = p.getString("pass", "");
  cfg.tz = p.getString("tz", DEFAULT_TZ);
  p.end();
  return cfg.ssid.length() > 0;
}

static void saveConfig(const ClockConfig &cfg) {
  Preferences p;
  p.begin("clock", false);
  p.putString("ssid", cfg.ssid);
  p.putString("pass", cfg.pass);
  p.putString("tz", cfg.tz);
  p.end();
}

// ---- BOOT button ------------------------------------------------------------

bool prov_button_long_press() {
  static bool inited = false, down = false;
  static uint32_t since = 0;
  if (!inited) {
    pinMode(BOOT_PIN, INPUT_PULLUP);
    inited = true;
  }
  if (digitalRead(BOOT_PIN) == LOW) {
    if (!down) {
      down = true;
      since = millis();
    } else if (millis() - since >= LONG_PRESS_MS) {
      down = false;
      return true;
    }
  } else {
    down = false;
  }
  return false;
}

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

static const char PAGE_HEAD[] = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Desk Clock Setup</title>
<style>body{font:16px system-ui,sans-serif;margin:auto;padding:20px;max-width:420px;background:#111;color:#eee}
h1{font-size:20px}label{display:block;margin:16px 0 4px;color:#aaa}
input,select,button{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border-radius:6px;border:1px solid #444;background:#222;color:#eee}
button{background:#0a84ff;border:0;margin-top:24px}small{color:#888}</style></head><body>)HTML";

static String buildForm(const String &netOptions, const ClockConfig &cur) {
  String h = PAGE_HEAD;
  h += "<h1>Desk Clock Setup</h1><form method=post action=/save>";
  h += "<label>WiFi network (2.4 GHz)</label>"
       "<input name=ssid list=nets required autocapitalize=off autocorrect=off value=\"" + esc(cur.ssid) +
       "\"><datalist id=nets>" + netOptions + "</datalist>";
  h += "<label>WiFi password</label>"
       "<input name=pass type=text autocapitalize=off autocorrect=off value=\"" + esc(cur.pass) + "\">";
  h += "<label>Timezone</label><select name=tz>";
  bool matched = false;
  for (const Zone &z : ZONES) {
    bool sel = cur.tz == z.posix;
    matched |= sel;
    h += String("<option value=\"") + esc(z.posix) + "\"" + (sel ? " selected" : "") + ">" + z.label + "</option>";
  }
  h += "</select><label>Custom timezone <small>(optional POSIX TZ, overrides the list)</small></label>"
       "<input name=custom autocapitalize=off autocorrect=off value=\"" + (matched ? String("") : esc(cur.tz)) + "\">";
  h += "<button type=submit>Save &amp; restart</button></form></body></html>";
  return h;
}

static String scanOptions() {
  String opts;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0 || opts.indexOf("\"" + esc(s) + "\"") >= 0) continue;  // hidden / duplicate
    opts += "<option value=\"" + esc(s) + "\">";
  }
  WiFi.scanDelete();
  return opts;
}

// ---- Setup mode -------------------------------------------------------------

[[noreturn]] void prov_run(bool hasSaved) {
  ClockConfig cur;
  prov_load(cur);

  // Scan before the AP goes up: scanning while serving would hop channels and
  // drop the client's connection.
  ui_set_status("Scanning...");
  lv_timer_handler();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  String netOptions = scanOptions();

  char apPass[9];
  snprintf(apPass, sizeof(apPass), "%08lu", (unsigned long)(esp_random() % 100000000UL));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, apPass);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Setup AP \"%s\" password %s at %s\n", AP_SSID, apPass, ip.toString().c_str());

  String qr = String("WIFI:T:WPA;S:") + AP_SSID + ";P:" + apPass + ";;";
  ui_show_setup(AP_SSID, apPass, qr.c_str(), ip.toString().c_str());

  DNSServer dns;
  dns.start(53, "*", ip);  // answer every hostname with our IP -> captive portal

  WebServer server(80);
  String page = buildForm(netOptions, cur);
  server.on("/", HTTP_GET, [&]() { server.send(200, "text/html", page); });
  server.on("/save", HTTP_POST, [&]() {
    ClockConfig n;
    n.ssid = server.arg("ssid");
    n.ssid.trim();
    n.pass = server.arg("pass");
    String custom = server.arg("custom");
    custom.trim();
    n.tz = custom.length() ? custom : server.arg("tz");
    if (n.ssid.length() == 0) {
      server.send(400, "text/plain", "WiFi network name is required.");
      return;
    }
    saveConfig(n);
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
