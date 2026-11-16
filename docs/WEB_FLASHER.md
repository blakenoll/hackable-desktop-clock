# Web flasher

`site/index.html` is a single static page, deployed to GitHub Pages by `.github/workflows/pages.yml` on every push to `main`. The workflow builds the firmware, copies the `.bin` files and the face renders (`docs/faces/faces.png`, `night.png`, `themes/`) into `site/`, and publishes it. Those build outputs are gitignored in `site/`.

## Flashing flow

- There's no ESP Web Tools UI. The page's own drawer (`<dialog id="flasher">`) calls `navigator.serial.requestPort()`, filtered to the Espressif, CH34x, CP210x and FTDI USB vendor IDs, with a "Show all ports" fallback.
- It then runs ESP Web Tools' `flash()` routine, loaded from esm.sh (`esp-web-tools@10.0.1/dist/flash.js`, esptool-js pinned to 0.4.7), and draws its state events as steps.
- The drawer can't be closed while flashing.
- `html.no-serial` (set in `<head>` when Web Serial is missing) swaps the Flash buttons for a "needs desktop Chrome" note.

## Theme preview

`PALETTES` in the page script lists the colour themes (name + current-second tick colour) and night colours (name + base colour) for the picker. It shows `faces/themes/day-N.png` / `night-N.png`, rendered by `docs/tools/sim/render.sh`. **Keep `PALETTES` in sync with `THEMES[]` and `NIGHTS[]` in `src/ui/theme.cpp`**: `python3 docs/tools/check.py` fails when they differ.

## Hero clock

The hero is a live DOM/SVG copy of the Polished face with a light 3D treatment:

- `#hero-clock` sets the perspective, and `.hc-body` tilts via `--rx` / `--ry` toward the pointer or with an idle sway (max 7°, subtle on purpose).
- Stacked `.hc-edge` discs give the case thickness. The ring, text and glare sit at small `translateZ` depths.
- On load there's a fade-in swing and a comet like `ui_boot_splash()`.
- All of this is off under `prefers-reduced-motion`.

## Credits

The footer credits adsb.fi (required by its terms) alongside the other data and font sources listed in the README.
