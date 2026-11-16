#pragma once
// THE list of user settings: NVS key, ClockConfig field, default and allowed range, one line each.
// config.cpp loads and saves NVS through it, and the setup page (setup/form.cpp) reads the submitted form
// through it, so the key, default and clamping can't drift apart between those three.
//
// Adding a setting:
//   1. a field in `struct ClockConfig` (config.h)
//   2. a line below (the NVS key is also the form field's `name`; NVS keys are at most 15 characters)
//   3. an input with that name in setup/form.cpp (field() / option() / onOff() build a <select>)
//   4. use it: display options go through uiOptions() in main.cpp into `UiOptions` (ui/ui.h)
//
// V is a visitor with text / secret / unit / flag / number methods (see Loader / Saver in config.cpp and
// FormReader in setup/form.cpp).
#include "config.h"
#include "tuning.h"
#include "ui/ui.h"

template <class V>
void config_fields(V &v, ClockConfig &c) {
  v.text("ssid", c.ssid, "");
  v.secret("pass", c.pass);  // never trimmed or printed
  v.text("tz", c.tz, DEFAULT_TZ);
  v.text("city", c.city, "");  // 5-digit US ZIP ("" = weather off)
  v.text("addr", c.addr, "");  // optional street address within the ZIP
  v.unit("units", c.units);    // 'F' / 'C'
  v.flag("h24", c.h24, false);
  v.number("datefmt", c.dateFmt, 0, 0, 1);
  v.number("accent", c.accent, 0, 0, ui_theme_count() - 1);  // colour theme (key predates themes)
  v.number("face", c.face, 0, 0, FACE_COUNT - 1);
  v.number("nmode", c.nightMode, 0, 0, 2);
  v.number("nstart", c.nightStart, 22, 0, 23);
  v.number("nend", c.nightEnd, 7, 0, 23);
  v.number("ncolor", c.nightColor, 0, 0, ui_night_color_count() - 1);  // NIGHTS[] in ui/theme.cpp
  v.flag("chime", c.chime, true);
  v.flag("detail", c.detail, true);
  v.flag("secs", c.seconds, false);
  v.text("tidestn", c.tideStation, "");
}
