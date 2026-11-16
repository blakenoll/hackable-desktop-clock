---
name: customize-clock
description: Change how the ESP32 desktop clock looks or behaves - time/date format, colors, themes, layout, faces, fonts, seconds ring, night mode, weather fields or icons, refresh intervals, timezone list, setup-page settings. Use when the user wants to tweak, restyle or extend the clock firmware.
---

# Customizing the clock

## Find the place

1. Check whether it's already a **setup-page option** (12/24 h, date order, face, theme, seconds digits, chime, detail line, night mode and colour, units, tide station). If it is, tell the user; no code change is needed.
2. Look the change up in the **"Where to change what"** table in `docs/CUSTOMIZING.md` (or the shorter map in `CLAUDE.md`). It names the file and function.
3. For background, read the doc the table links: `docs/FACES.md` (faces, rings, `FaceDef` hooks), `docs/ARCHITECTURE.md` (threads, the rebuild model, night mode, motion), `docs/DATA.md` (weather / tides / radar), `src/ui/fonts/README.md` (fonts, icons).

The quick map: timings and thresholds are in `src/tuning.h`; settings in `src/config_fields.h`; the setup page in `src/setup/`; one file per face in `src/ui/faces/`; themes in `src/ui/theme.cpp`; weather text and icons in `src/data/weather_format.cpp`.

## Workflow

1. Make the change in the place the doc names, small and in the existing style.
2. `pio run` (see the `flash-clock` skill for PATH and Rosetta). Report the flash-usage line.
3. For anything visual, run `sh docs/tools/sim/render.sh` and look at `docs/faces/*.png` yourself before asking the user to flash. It renders the real UI code.
4. Ask before flashing; use the `flash-clock` skill to do it. You can't see the real screen, so tell the user what to check and expect a round or two of nudging.
5. Update the doc that describes what you changed (and `PALETTES` in `site/index.html` for theme changes), then run `python3 docs/tools/check.py`.

## Constraints

The rules in `CLAUDE.md` apply. The ones customizations hit most:

- **Round bezel:** the usable width at offset `y` is about `2*sqrt(120² - y²)` px.
- **Night parity:** every day colour needs a night value via `mixDN(day, nightC(pct))`.
- **Font subsets:** a glyph outside a font's range doesn't render. `manrope_caps_12` is uppercase-only.
- **Faces:** shared code doesn't branch on `opt.face`, and pointers may be null.
- **LVGL:** only the main loop calls it.
- **Downloads:** put downloaded fonts in a new, empty folder outside the repo.
- Never print saved credentials or the street address.
