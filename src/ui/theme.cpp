#include "ui_internal.h"

namespace ui {

UiOptions opt;
bool night = false;
uint8_t nightMix = 0;

const Theme THEMES[] = {
    {0xFFFFFF, 0xFF4D4D, 0xFF3030, 0x9AA3AD, 0x6B7480, 0x56606D, 0x8E9EB3, 0x2A3138},  // Red
    {0xFFF1D6, 0xFFB020, 0xFFA010, 0xB0A088, 0x8A775C, 0x6B5E48, 0xB09A70, 0x332C1F},  // Amber
    {0xE8FFF1, 0x4CE08A, 0x30E070, 0x8FAA9A, 0x5F7A6B, 0x4C6557, 0x82AD94, 0x1F2E26},  // Mint
    {0xE3F4FF, 0x36D6F0, 0x20D0F0, 0x8AA4B5, 0x5E7A8C, 0x4A6272, 0x7FA6BC, 0x1F2D36},  // Ice
    {0xFFE8F5, 0xFF6EC7, 0xFF4DB8, 0xB091A4, 0x86677A, 0x695062, 0xB085A0, 0x30222B},  // Orchid
    {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x9AA3AD, 0x6B7480, 0x56606D, 0x8E9EB3, 0x2A3138},  // Mono
    {0xFFEBD6, 0xFF7A3D, 0xFF6A28, 0xB09A88, 0x8A6F5C, 0x6B5648, 0xB08A70, 0x33291F},  // Ember
};

const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// Night colours. Everything shown at night is this colour at some brightness (nightC(percent)).
// Night colours. Everything shown at night is this colour at some brightness (nightC(percent)).
static const uint32_t NIGHTS[] = {0xD01818, 0xC8780A, 0x6E6E6E};  // red, amber, dim white

const int NIGHT_COUNT = sizeof(NIGHTS) / sizeof(NIGHTS[0]);

uint32_t scaleHex(uint32_t c, int pct) {
  uint32_t r = ((c >> 16) & 0xFF) * pct / 100, g = ((c >> 8) & 0xFF) * pct / 100, b = (c & 0xFF) * pct / 100;
  return (r << 16) | (g << 8) | b;
}

uint32_t mixHex(uint32_t a, uint32_t b, int pctA) {  // pctA = 100 -> a
  auto ch = [&](int sh) { return (((a >> sh) & 0xFF) * pctA + ((b >> sh) & 0xFF) * (100 - pctA)) / 100; };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

uint32_t nightC(int pct) { return scaleHex(NIGHTS[opt.nightColor], pct); }

// Day/night pair blended by nightMix, so a day->night switch can fade instead of snapping.
lv_color_t mixDN(uint32_t day, uint32_t nt) {
  return lv_color_mix(lv_color_hex(nt), lv_color_hex(day), nightMix);
}

}  // namespace ui
