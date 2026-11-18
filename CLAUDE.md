# CLAUDE.md

ESP32-C3 SuperMini + 240x240 GC9A01 round display: an LVGL 9 desk clock with NTP time, weather, tides, an aircraft radar, ten clock faces and night mode. People customize their own clock, often through an AI agent. Keep the code where the map below says it is, and keep the docs true when you change it.

## Where to change what

| To change... | Go to |
|---|---|
| Refresh intervals, timeouts, thresholds (weather, tides, radar, WiFi, button) | `src/tuning.h` |
| A setup-page setting (add one, its default or range) | `src/config_fields.h` (checklist at the top) |
| Setup page wording / layout / styling | `src/setup/form.cpp` |
| Timezone list | `ZONES[]` in `src/setup/zones.cpp` |
| Setup access point, captive portal, WiFi scan | `src/setup/portal.cpp` |
| Settings API over WiFi (JSON, mDNS, API key) | `src/api/api.cpp` (see [docs/API.md](docs/API.md)) |
| Home Assistant integration | `custom_components/deskclock/`, tests in `tests/home_assistant/` (see [docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md)) |
| One clock face | `src/ui/faces/face_<name>.cpp` (see [docs/FACES.md](docs/FACES.md)) |
| Add a face | new `face_<name>.cpp` + `enum Face` in `src/ui/ui.h` + table in `src/ui/faces.cpp` |
| Colour themes / night colours | `THEMES[]` / `NIGHTS[]` in `src/ui/theme.cpp` (and `PALETTES` in `site/index.html`) |
| Shared face pieces: time and date, weather row, seconds ring, fades | `src/ui/time_view.cpp`, `weather_view.cpp`, `ring.cpp`, `transitions.cpp` (file list at the top of `src/ui/ui_internal.h`) |
| Boot animations, setup screen on the display | `src/ui/boot_screens.cpp`, `src/ui/setup_screen.cpp` |
| Weather icons and text ("40% rain", "NW 12 MPH") | `src/data/weather_format.cpp` |
| Fetching weather / tides / aircraft | `src/data/weather.cpp`, `tide.cpp`, `aircraft.cpp` (see [docs/DATA.md](docs/DATA.md)) |
| Fonts | `src/ui/fonts/` ([README](src/ui/fonts/README.md) there) |
| Pins, display, board, button pin | `platformio.ini`, `src/hw/` (see [docs/HARDWARE.md](docs/HARDWARE.md)) |
| Boot flow, main loop | `src/main.cpp` |
| Web flasher site | `site/index.html` (see [docs/WEB_FLASHER.md](docs/WEB_FLASHER.md)) |

Docs: [CUSTOMIZING](docs/CUSTOMIZING.md) (recipes by task), [ARCHITECTURE](docs/ARCHITECTURE.md) (boot, tasks, threading, the face rebuild model, night mode), [FACES](docs/FACES.md), [DATA](docs/DATA.md), [API](docs/API.md), [HOME_ASSISTANT](docs/HOME_ASSISTANT.md), [HARDWARE](docs/HARDWARE.md), [TROUBLESHOOTING](docs/TROUBLESHOOTING.md), [WEB_FLASHER](docs/WEB_FLASHER.md). Skills in `.claude/skills/`: `flash-clock`, `customize-clock`, `change-hardware`. `AGENTS.md` and `.agents/skills` are symlinks to this file and that folder, so edit the real files.

## Commands

- Build: `pio run`. Flash: `pio run -t upload` (ask first). With [mise](https://mise.jdx.dev): `mise install`, then `mise run build|upload|clean|render|check`.
- `pio` lives in `~/.platformio/penv/bin` and may not be on PATH in agent shells: `export PATH="$PATH:$HOME/.platformio/penv/bin"`.
- Host renders: `sh docs/tools/sim/render.sh` compiles `src/ui/` for this computer and writes `docs/faces/*.png` (needs gcc, python3 and one prior `pio run` for LVGL, or `LVGL_DIR=<lvgl 9.6 checkout>`). Use it to check layout before flashing, and **rerun it after any change to how a face, theme or colour looks** (even a small brightness tweak) and commit the updated `docs/faces/` PNGs (and `custom_components/deskclock/brand/`, the Home Assistant icon) with it; the web flasher, docs and Home Assistant show those images.
- Home Assistant integration tests: `pip install -r tests/home_assistant/requirements.txt && pytest tests/home_assistant` (Python 3.14; CI runs them). Run them after changing the API or the integration.
- Consistency check: `python3 docs/tools/check.py`. It confirms that paths and functions named in the docs exist and that the web flasher's palettes match `theme.cpp`. CI runs it; run it after moving or renaming anything.

## Rules

- **Flashing and installing system software need the user's OK**, unless they just asked for it.
- **Only the main loop (and setup mode) call LVGL.** Background tasks (weather, tides, radar) publish mutex-protected snapshots (`weather_get()`...) that `loop()` copies into the UI.
- **Never print or commit credentials or the street address.** They live in NVS on the board, not in the repo. That includes the settings API key (`apitoken`).
- **Faces:** shared code never branches on `opt.face`; add a `FaceDef` hook or flag instead. Face object pointers may be null (use `tc` / `setText` / `setHidden` / `swapText` or check). A face's private statics are stale while another face is active, so shared code reaches them only through hooks. Animations of non-object variables and `lv_timer`s must be stopped from `rebuild()`. Details: [docs/FACES.md](docs/FACES.md).
- **Night colours** come from `nightC(pct)` and are blended with `mixDN(day, night)`; never hard-code a night hex. Every day colour needs a night counterpart. Keep night tones warm, because blue and green light hurts night vision.
- **Round screen:** at vertical offset `y` from the centre there are about `2*sqrt(120² - y²)` px of width; the seconds ring takes the outer ~12 px. Fonts are glyph subsets (`manrope_caps_12` has no lowercase), so check the range in the file header.
- **Network:** one HTTPS session at a time (`NetLock`, `src/data/net.h`); keep background tasks at priority 1. See [docs/DATA.md](docs/DATA.md).
- **Don't remove `fix_tft.py` or bump the pinned platform/libraries** in `platformio.ini` without checking that the display still initializes ([docs/HARDWARE.md](docs/HARDWARE.md)).
- **Keep docs true:** a change that moves, renames or adds behaviour updates the doc that describes it in the same commit. Put a fact in one place and link to it rather than copying numbers around.

## Gotchas (build, flash, serial; all hit during bring-up)

- **Apple Silicon needs Rosetta 2.** The pinned `espressif32@6.10.0` (Arduino core 2.x) uses an x86_64-only `toolchain-riscv32-esp@8.4.0`; without Rosetta: `Bad CPU type in executable`. Install: `softwareupdate --install-rosetta --agree-to-license` (the user runs it).
- **`fix_tft.py` is load-bearing.** It is a pre-build script that patches TFT_eSPI's `SPI_PORT` from `SPI2_HOST` (1) to `2`, because this core's `REG_SPI_BASE(i)` only maps `i==2`. Without it `tft.init()` hits a store-access fault (`MTVAL 0x10`) and the board boot-loops.
- **Don't run `pio device monitor` from an agent shell** (it needs a TTY). Read serial with pyserial from `~/.platformio/penv/bin/python` on `/dev/cu.usbmodem*` at 115200; toggle RTS to reset and catch boot logs.
- **A serial port has one user.** A lingering monitor makes upload fail (`Invalid head of packet`). Check `lsof /dev/cu.usbmodem*`. Don't kill a process you didn't start; ask.
- **Upload `No serial data received`** with the port present and free (USB-Serial-JTAG auto-reset is intermittently unreliable): try a normal upload first. Then start a background retry loop of `PLATFORMIO_UPLOAD_FLAGS="--before=no_reset" pio run -t upload` until `[SUCCESS]`, and have the user unplug, hold BOOT, plug in and keep holding ~15 s. The chip reboots when BOOT is released, so the flash must already be connecting. Boot loops also make USB flaky; the same BOOT recipe helps.
- **Decoding crashes:** `~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-addr2line -pfiaC -e .pio/build/lolin_c3_mini/firmware.elf <MEPC RA stack addrs...>`.
- **Force setup mode** on a flashed board: hold BOOT 3 s while it runs. Holding it *during reset* enters the ROM bootloader instead.
- Harmless build `#warning`s: no TFT_eSPI DMA on the C3, and `TOUCH_CS` not defined.
- **Preview night mode:** build with `PLATFORMIO_BUILD_FLAGS="-DFORCE_NIGHT=1"` (or `-DCYCLE_NIGHT=1` to flip every 75 s), then **reflash a normal build**.
