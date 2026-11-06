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
- Flash use is ~93% of the default 1.3 MB app partition (~97 KB free; the 48 pt Montserrat font is the big item). Check headroom before adding fonts or features; a custom partition table (`board_build.partitions`) would free more.
- Flashing hardware and installing system software are user-confirmed actions. Ask before doing either unless they've just asked for it.
