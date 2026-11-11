#include "../ui_internal.h"

namespace ui {

// Light digits and little else, one orbiting dot.
static void build() {
  mkWeather(0, -54, false, 0, &manrope_20, &weather_icons);
  mkTime(0, &manrope_light_64);
  mkDateRow(50, 6, 2);
  mkOffline(0, -88);
}

static void palette(Palette &p, const Theme &T) {  // monochrome
  p.day = p.sub;
  p.weather = p.sub;
  p.icon = mixDN(T.sub, nightC(69));
}

static FaceDef make() {
  FaceDef f;
  f.ring = K_ORBIT;
  f.build = build;
  f.date = dateQuietLine;
  f.palette = palette;
  return f;
}
extern const FaceDef faceDefZen = make();

}  // namespace ui
