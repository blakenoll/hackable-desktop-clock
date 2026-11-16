# Fonts

Generated LVGL font files (C arrays) made with [`lv_font_conv`](https://github.com/lvgl/lv_font_conv). They are glyph **subsets**: a character outside a font's range doesn't render. Each file's header comment has the exact command it was made with. Every font costs flash, so subset new ones too.

| File | LVGL name | Source | Size | Glyphs | Used for |
|---|---|---|---|---|---|
| `font_manrope_14.c` | `manrope_14` | Manrope 500 | 14 | `0x20-0x7E`, `°` | small text, weather messages |
| `font_manrope_20.c` | `manrope_20` | Manrope 500 | 20 | `0x20-0x7E`, `°` | temperature, status text |
| `font_manrope_caps_12.c` | `manrope_caps_12` | Manrope 700 | 12 | `0x20-0x5F` (**no lowercase**: uppercase text first), `°`, `·` | dates, detail line, labels |
| `font_manrope_34.c` | `manrope_34` | Manrope 600 | 34 | digits, `:` `-` space `°` | Forecast, Tide and Radar time; Forecast temperature |
| `font_manrope_56.c` | `manrope_56` | Manrope 600 | 56 | digits, `:`, space | the time on most faces |
| `font_manrope_74.c` | `manrope_74` | Manrope 700 | 74 | digits | Stacked |
| `font_manrope_light_64.c` | `manrope_light_64` | Manrope 300 | 64 | digits, `:` | Zen |
| `font_weather.c` | `weather_icons` | Weather Icons | 26 | 12 icons (see `weather_icon()`) | weather icon |

LVGL's built-in `lv_font_montserrat_14` (enabled in `include/lv_conf.h`) is used only for the offline WiFi symbol (`LV_SYMBOL_WIFI`).

To use a font in UI code, declare it in `src/ui/ui_internal.h` (`LV_FONT_DECLARE`).

## Regenerating

Download sources into a **new, empty folder outside the repo** (treat downloads as untrusted; don't run tools from inside it). Node is needed (`mise install` provides it).

**Manrope** (SIL OFL): static weights from the `@fontsource/manrope` npm package (`files/manrope-latin-<weight>-normal.woff`):

```
npx lv_font_conv --font /path/manrope-latin-500-normal.woff --size 14 --bpp 4 --no-compress \
  --format lvgl --lv-include lvgl.h --lv-font-name manrope_14 -r 0x20-0x7E,0xB0 -o src/ui/fonts/font_manrope_14.c
```

**Weather Icons** (SIL OFL): `weathericons-regular-webfont.ttf` and `weather-icons.css` from [erikflowers/weather-icons](https://github.com/erikflowers/weather-icons). Find a glyph's codepoint in the CSS (e.g. `.wi-day-sunny:before { content: "\f00d"; }`) and list **every** glyph you want:

```
npx lv_font_conv --font /path/weathericons-regular-webfont.ttf --size 26 --bpp 4 --no-compress --no-kerning \
  --format lvgl --lv-include lvgl.h --lv-font-name weather_icons \
  -r 0xf00d,0xf02e,0xf002,0xf086,0xf013,0xf014,0xf01c,0xf019,0xf0b5,0xf01b,0xf01a,0xf01e \
  -o src/ui/fonts/font_weather.c
```

Then use the glyph in `weather_icon()` (`src/data/weather_format.cpp`) as `"\uF0xx"`. Size 26 fits the weather row; 32 clipped at the top. Keep the SIL OFL credits in the README.
