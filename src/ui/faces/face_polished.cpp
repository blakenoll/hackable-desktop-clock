#include "../ui_internal.h"

namespace ui {

// Default face: date / big time / weather with range bar / detail. Dotted seconds ring.
static void build() {
  mkDateRow(-62, 7, 2);
  mkTime(0, &manrope_56);
  mkWeather(0, 62, false, 1, &manrope_20, &weather_icons);
  mkDetail(0, 90);
  mkOffline(0, -88);
}

static void palette(Palette &p, const Theme &T) { p.timeC = mixDN(T.colon, nightC(60)); }  // dim colon

static FaceDef make() {
  FaceDef f;
  f.ring = K_DOTTICKS;
  f.build = build;
  f.palette = palette;
  return f;
}
extern const FaceDef faceDefPolished = make();

}  // namespace ui
