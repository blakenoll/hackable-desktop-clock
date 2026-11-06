# CLAUDE.md

ESP32-C3 (Lolin C3 Mini) + 240x240 GC9A01 round display, LVGL 9 analog clock, NTP time. See `README.md` for wiring and code layout.

## Commands

- Build: `pio run`
- Flash: `pio run -t upload`
- `pio` lives in `~/.platformio/penv/bin`. It may not be on PATH in agent shells, so use `export PATH="$PATH:$HOME/.platformio/penv/bin"` or the full path.

## Gotchas (all hit during first bring-up)

- **Apple Silicon needs Rosetta 2.** The pinned `espressif32` platform (6.10.0, Arduino core 2.x) uses an x86_64-only `toolchain-riscv32-esp@8.4.0`. Without Rosetta the build fails with `Bad CPU type in executable`. Install: `softwareupdate --install-rosetta --agree-to-license` (user must run it).
- **WiFi/timezone are runtime settings, not build-time.** They're stored in NVS (`Preferences`, namespace `clock`: `ssid`, `pass`, `tz`) and set through the captive portal in `src/provisioning.cpp`. `include/secrets.h` is obsolete (still gitignored in case an old copy exists). Never print or commit stored credentials. To force setup mode on a flashed board: hold BOOT 3 s while running (holding it *during reset* enters the ROM bootloader instead).
- **`fix_tft.py` is load-bearing.** It is a PlatformIO pre-build script (`extra_scripts` in `platformio.ini`) that patches the downloaded TFT_eSPI. The library sets `SPI_PORT` to `SPI2_HOST` (=1), but this core's `REG_SPI_BASE(i)` only maps `i==2` to the SPI2 base, so the base is 0. The result is a store-access fault (`MTVAL 0x10`) in `tft.init()` and a boot loop. The script rewrites it to `2`. Don't remove it unless the platform or core is upgraded and verified.
- **Don't use `pio device monitor` from an agent shell.** It needs an interactive TTY. To read serial output, use pyserial from the PlatformIO venv at `~/.platformio/penv/bin/python` on `/dev/cu.usbmodem*` at 115200. Toggle RTS to reset the board and catch boot logs. Have the user run the monitor in their own terminal if they want it live.
- **A serial port can only have one user.** A lingering monitor makes `upload` fail with `Invalid head of packet` or serial-disconnect errors. Check `lsof /dev/cu.usbmodem*` first. Don't kill a process you didn't start. Ask the user.
- **Boot loops make USB flaky.** If upload can't connect, have the user hold BOOT, tap RST, release BOOT (download mode), then retry.
- **Upload sometimes fails with `No serial data received`** even though the port is present and free (the USB-Serial-JTAG auto-reset is intermittently unreliable; cause unconfirmed). Tapping RST often doesn't enter download mode. Recipe that worked: start a background retry loop with `PLATFORMIO_UPLOAD_FLAGS="--before=no_reset" pio run -t upload` (repeat until `[SUCCESS]`), then have the user unplug USB, hold BOOT, plug in while holding, and keep holding ~15 s so the flash connects before BOOT is released. The chip reboots when BOOT is released, so the flash must already be connecting. Try the normal auto-reset upload first.
- **Decoding crashes:** `~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-addr2line -pfiaC -e .pio/build/lolin_c3_mini/firmware.elf <addr...>` on the `MEPC`, `RA` and stack addresses from a Guru Meditation dump.
- Strapping pins GPIO 2/8/9 shouldn't be used for the display. GPIO 9 (the BOOT button) is only read as an input after boot, for the setup long-press. The C3 has no TFT_eSPI DMA support, and the build prints `#warning`s for that and for `TOUCH_CS`. Both are harmless.

## Conventions

- Display pins and SPI settings are `-D` build flags in `platformio.ini`, not edits to the library's `User_Setup.h`.
- 2.4 GHz WiFi only (C3 has no 5 GHz). Timezone is a POSIX TZ string; the portal offers presets plus a custom field.
- The setup AP uses a random 8-digit WPA2 password per session (shown on screen + in the QR code, and printed to serial). The scan runs *before* the AP starts, since scanning while serving drops clients.
- Flash: `board_build.partitions = huge_app.csv` (~3 MB app, no OTA slot, NVS unchanged at 0x9000 so saved settings survive the switch). Firmware is ~1.37 MB (~44%). The weather/TLS code alone cost ~158 KB, which is why the default 1.3 MB partition was no longer enough. The 48 pt Montserrat font (~92 KB) could be subset to digits+`:` if space gets tight again.
- **Weather** (`src/weather.cpp`): Open-Meteo, no API key. A FreeRTOS task geocodes the saved US ZIP code once (stored in the `city` NVS key; cached as `geocity`/`lat`/`lon`, redone if it changes; `countryCode=US`; Open-Meteo can't parse free text like `Charleston SC`, and a 5-digit ZIP is unambiguous), then fetches current conditions every 15 min (retry 60 s). HTTPS with `setInsecure()` (no cert check, by choice: public read-only data). The display shows a weather icon + temperature (e.g. `[sun] 72F`; fonts have no `°`). Icons come from `src/font_weather.c`, a generated subset of Weather Icons (OFL); to add/change glyphs regenerate it with `npx lv_font_conv --font weathericons-regular-webfont.ttf --size 32 --bpp 4 --no-compress --no-kerning --format lvgl --lv-include lvgl.h --lv-font-name weather_icons -r <codepoints> -o src/font_weather.c` (font from github.com/erikflowers/weather-icons; keep downloads in their own dir). WMO code -> glyph/colour mapping is `weather_icon()` in `weather.cpp`; night uses `is_day` from the API. Empty ZIP disables weather. Not-found shows `ZIP not found`.
- Flashing hardware and installing system software are user-confirmed actions. Ask before doing either unless they've just asked for it.
- **Night mode** (`ui_set_night`): driven by `is_day` from the weather fetch (so it needs a ZIP). All text, the weather icon and the seconds ring switch to red tones; the last state is persisted in NVS (`night`) so power-up matches. The QR setup screen is deliberately unaffected. To preview it without waiting for sunset, build and flash with `PLATFORMIO_BUILD_FLAGS="-DFORCE_NIGHT=1"` (pins night on, persists nothing), then **reflash a normal build** (unset the variable) when done.
- **Seconds ring**: 60 `lv_line` ticks created in `createTicks()`; only changed ticks are restyled each second. Layout constants (ring radius, label y-offsets of ±58) were tuned by eye on the real display.
