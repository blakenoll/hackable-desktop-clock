// Setup mode: scans WiFi, starts a WPA2 access point with a random password, and serves the setup page
// (form.cpp) through a captive portal until the user saves, then reboots. Never returns.
#include "portal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <lvgl.h>
#include "setup_internal.h"
#include "../tuning.h"
#include "../ui/ui.h"

// Async scan so the sonar animation keeps running (lv_timer_handler) while it works.
static String scanOptions() {
  WiFi.scanNetworks(true);
  uint32_t t0 = millis();
  int n;
  while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - t0 < SETUP_SCAN_TIMEOUT_MS) {
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
    if (ssid.length() == 0 || opts.indexOf("value=\"" + html_esc(ssid) + "\"") >= 0) continue;
    bool open = WiFi.encryptionType(best) == WIFI_AUTH_OPEN;
    opts += setup_network_option(ssid, open);
  }
  WiFi.scanDelete();
  return opts;
}

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
  snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", SETUP_AP_PREFIX, (unsigned)((efuse >> 32) & 0xFF),
           (unsigned)((efuse >> 40) & 0xFF));
  WiFi.softAP(apSsid, apPass);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Setup AP \"%s\" password %s at %s\n", apSsid, apPass, ip.toString().c_str());

  ui_show_setup(apSsid, apPass, ip.toString().c_str());

  DNSServer dns;
  dns.start(53, "*", ip);  // answer every hostname with our IP -> captive portal

  WebServer server(80);
  String page = setup_page(netOptions, cur);
  server.on("/", HTTP_GET, [&]() { server.send(200, "text/html", page); });
  server.on("/save", HTTP_POST, [&]() {
    ClockConfig n;
    setup_parse(server, n);
    if (n.ssid.length() == 0) {
      server.send(400, "text/plain", "WiFi network name is required.");
      return;
    }
    config_save(n);
    server.send(200, "text/html", setup_saved_page(n.ssid));
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
    if (hasSaved && millis() - lastActive > SETUP_IDLE_REBOOT_MS) ESP.restart();  // retry saved WiFi
    delay(5);
  }
}
