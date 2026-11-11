#include "../ui_internal.h"

namespace ui {

// Seconds as one sweeping arc, two-line date ("TUESDAY" / "NOVEMBER 10"), high/low as plain numbers.
static void build() {
  dayLabel = mkLabelAt(&manrope_caps_12, 3, 0, -66);
  dateLabel = mkLabelAt(&manrope_caps_12, 2, 0, -49);
  mkTime(2, &manrope_56);
  mkWeather(0, 60, false, 2, &manrope_20, &weather_icons);
  mkDetail(0, 88);
  mkOffline(0, -88);
}

static FaceDef make() {
  FaceDef f;
  f.ring = K_ARC;
  f.build = build;
  f.fullDate = true;
  return f;
}
extern const FaceDef faceDefArc = make();

}  // namespace ui
