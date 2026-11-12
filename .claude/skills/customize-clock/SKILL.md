---
name: customize-clock
description: Change how the ESP32 desktop clock looks or behaves - time/date format, colors, layout, fonts, seconds ring, night mode, weather fields or icons, timezone list, setup-page settings. Use when the user wants to tweak, restyle or extend the clock firmware.
---

# Customizing the clock

Start from `docs/CUSTOMIZING.md`: it lists the exact file and function for each common change (12/24-hour, date format, colors table, layout offsets, ring constants, timezone presets, weather refresh and fields, adding a setup-page setting, regenerating the icon font). `docs/HARDWARE.md` covers pins/displays and `CLAUDE.md` has project facts.

## Workflow

1. Make the change in the file named in the doc. Keep it small and in the existing style (the UI is `src/ui/`, one file per face in `src/ui/faces/`; weather is `src/data/weather.cpp`, shown via `src/data/weather_format.cpp`; saved settings are `src/config.cpp`; the setup page is `src/provisioning.cpp`).
2. `pio run` (see the `flash-clock` skill for PATH and Rosetta notes). Report the flash-usage line.
3. Ask before flashing; use the `flash-clock` skill to do it.
4. **You can't see the screen.** Tell the user what to check and ask how it looks; expect a round or two of nudging offsets/sizes.
5. Update `README.md` / `docs/` if behavior or settings changed.

## Constraints to respect

- **Round bezel.** Content near the corners is clipped. Usable width at vertical offset `y` from center is about `2*sqrt(120^2 - y^2)` px; the seconds ring occupies the outer ~12 px.
- **Only the main loop calls LVGL.** The weather task communicates through `weather_get()` (mutex-protected struct). Never call `lv_*` from another task.
- **Loading states** (WiFi glyph, weather skeleton) are animated by LVGL timers/anims, so any blocking wait must keep calling `lv_timer_handler()`. Their colours need night variants too.
- **macOS tooling.** `sed -i` needs `sed -i ''`; for multi-line edits prefer the Edit tool or `python3`. Symbols like `LV_SYMBOL_UP` aren't in custom fonts, so give a custom font `.fallback = &lv_font_montserrat_14`.
- **Night mode parity.** Every day color needs a red-tone night counterpart in `ui_set_night()` / `styleTick()`; avoid blue/green at night. The setup screen is intentionally excluded.
- **Fonts cost flash.** Fonts are generated Manrope files in `src/ui/fonts/font_manrope_*.c` (see `docs/CUSTOMIZING.md`); a new size means generating a new file. Budget: firmware ~1.29 MB of a 3 MB partition.
- **Icon font changes** need the `lv_font_conv` procedure in `docs/CUSTOMIZING.md`. Download the source font into a new empty folder outside the repo, run tools from elsewhere, and keep the SIL OFL credit in the README.
- **Settings added to the setup page** touch four places in `src/provisioning.*` (struct, load, save, form + `/save` handler); see the doc.
- **Don't remove `fix_tft.py`** or bump the pinned platform/library versions in `platformio.ini` without verifying the display initializes (see `docs/HARDWARE.md`).
- Never commit or print saved credentials; they live in the board's flash, not in the repo.
