// The setup page served in setup mode (portal.cpp): plain HTML built from strings, one *Fields() helper per
// section, and setup_parse() which reads the submitted form back. To add a setting, see config_fields.h.
#include "setup_internal.h"
#include "../config_fields.h"
#include "../ui/ui.h"

String html_esc(const String &s) {
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
  bool known = netOptions.indexOf("value=\"" + html_esc(cur.ssid) + "\"") >= 0 && cur.ssid.length() > 0;
  String opts = netOptions;
  if (known) opts.replace("value=\"" + html_esc(cur.ssid) + "\"", "value=\"" + html_esc(cur.ssid) + "\" selected");
  return "<label>WiFi network (2.4 GHz)</label>"
         "<select id=sel><option value=\"\">Choose a network...</option>" + opts +
         "<option value=\"__other\"" + (!known && cur.ssid.length() ? " selected" : "") + ">Other (type manually)...</option></select>"
         "<input id=ssid name=ssid required autocapitalize=off autocorrect=off placeholder=\"Network name\" value=\"" + html_esc(cur.ssid) + "\">"
         "<script>var s=document.getElementById('sel'),i=document.getElementById('ssid');"
         "function u(){var o=s.value==='__other'||s.value==='';i.style.display=o?'block':'none';"
         "if(s.value&&!o)i.value=s.value;else if(s.value==='__other'&&i.value&&[].some.call(s.options,function(x){return x.value===i.value}))i.value='';}"
         "s.onchange=function(){u();if(s.value==='__other')i.focus()};"
         "if(s.value&&s.value!=='__other')i.value=s.value;u();</script>"
         "<label>WiFi password</label>"
         "<input name=pass type=text autocapitalize=off autocorrect=off value=\"" + html_esc(cur.pass) + "\">";
}

static String timezoneField(const ClockConfig &cur) {
  String opts;
  bool matched = false;
  for (int i = 0; i < ZONE_COUNT; i++) {
    const Zone &z = ZONES[i];
    bool sel = cur.tz == z.posix;
    matched |= sel;
    opts += String("<option value=\"") + html_esc(z.posix) + "\"" + (sel ? " selected" : "") + ">" + z.label + "</option>";
  }
  // A previously saved zone that isn't in the presets stays selectable instead of silently resetting.
  if (!matched && cur.tz.length()) opts += "<option value=\"" + html_esc(cur.tz) + "\" selected>Saved: " + html_esc(cur.tz) + "</option>";
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
  String nightColors;
  for (int i = 0; i < ui_night_color_count(); i++) nightColors += option(i, cur.nightColor, ui_night_color_name(i));
  return field("Night mode", "nmode",
               option(0, cur.nightMode, "After sunset (needs ZIP)") + option(1, cur.nightMode, "Fixed hours") + option(2, cur.nightMode, "Never")) +
         hoursField("Fixed hours: night starts", "nstart", cur.nightStart, cur.h24) +
         hoursField("Fixed hours: night ends", "nend", cur.nightEnd, cur.h24) +
         field("Night colour", "ncolor", nightColors);
}

static String weatherFields(const ClockConfig &cur) {
  // Address above ZIP, the way addresses are written. Street only: the ZIP already fixes the city and state.
  return "<label>Street address <small>(optional, e.g. 123 Main St; with the ZIP below, pins weather, tides and radar to your "
         "house instead of the middle of the ZIP; looked up once via the US Census geocoder)</small></label>"
         "<input name=addr maxlength=100 autocomplete=address-line1 value=\"" + html_esc(cur.addr) + "\">" +
         "<label>ZIP code for weather <small>(optional, US 5-digit)</small></label>"
         "<input name=city inputmode=numeric autocomplete=postal-code pattern=\"[0-9]{5}\" maxlength=5 title=\"5-digit US ZIP code\" value=\"" +
         html_esc(cur.city) + "\">" +
         field("Temperature units", "units", String("<option value=F") + (cur.units == 'F' ? " selected" : "") + ">Fahrenheit</option><option value=C" +
                                                 (cur.units == 'C' ? " selected" : "") + ">Celsius</option>") +
         "<label>NOAA tide station <small>(optional, station ID for the Tide face, e.g. 8665530; blank = nearest" +
         (cur.tideGeo == config_geo_key(cur) && cur.tideAuto.length() ? ", now " + html_esc(cur.tideAuto) + " " + html_esc(cur.tideName) : String("")) +
         ")</small></label><input name=tidestn pattern=\"[A-Za-z0-9]{4,10}\" maxlength=10 title=\"NOAA station ID from tidesandcurrents.noaa.gov\" value=\"" +
         html_esc(cur.tideStation) + "\">";
}

String setup_page(const String &netOptions, const ClockConfig &cur) {
  return String(PAGE_HEAD) + "<h1>Desk Clock Setup</h1><form method=post action=/save>" + wifiFields(netOptions, cur) +
         timezoneField(cur) + displayFields(cur) + nightFields(cur) + weatherFields(cur) +
         "<button type=submit>Save &amp; restart</button></form></body></html>";
}

String setup_saved_page(const String &ssid) {
  return String(PAGE_HEAD) + "<h1>Saved</h1><p>The clock is restarting and will join <b>" + html_esc(ssid) +
         "</b>. You can close this page.</p></body></html>";
}

String setup_network_option(const String &ssid, bool open) {
  return "<option value=\"" + html_esc(ssid) + "\">" + html_esc(ssid) + (open ? "" : " \xF0\x9F\x94\x92") + "</option>";
}

// Visitor for config_fields(): a missing field keeps the default; numbers are clamped.
struct FormReader {
  WebServer &s;
  void text(const char *k, String &f, const char *def) {
    f = s.hasArg(k) ? s.arg(k) : String(def);
    f.trim();
  }
  void secret(const char *k, String &f) { f = s.arg(k); }
  void unit(const char *k, char &f) { f = s.arg(k) == "C" ? 'C' : 'F'; }
  void flag(const char *k, bool &f, bool def) { f = s.hasArg(k) ? s.arg(k) == "1" : def; }
  void number(const char *k, uint8_t &f, uint8_t def, int lo, int hi) {
    f = s.hasArg(k) ? (uint8_t)constrain(s.arg(k).toInt(), lo, hi) : def;
  }
};

void setup_parse(WebServer &server, ClockConfig &n) {
  FormReader r{server};
  config_fields(r, n);
}
