# CLAUDE.md

ESP32-C3 (Lolin C3 Mini) + 240x240 GC9A01 round display, LVGL 9 analog clock, NTP time. See `README.md` for wiring and code layout.

## Commands

- Build: `pio run`
- Flash: `pio run -t upload`
- `pio` lives in `~/.platformio/penv/bin`. It may not be on PATH in agent shells, so use `export PATH="$PATH:$HOME/.platformio/penv/bin"` or the full path.

## Gotchas (all hit during first bring-up)

- **Apple Silicon needs Rosetta 2.** The pinned `espressif32` platform (6.10.0, Arduino core 2.x) uses an x86_64-only `toolchain-riscv32-esp@8.4.0`. Without Rosetta the build fails with `Bad CPU type in executable`. Install: `softwareupdate --install-rosetta --agree-to-license` (user must run it).
- **`include/secrets.h` is gitignored and required.** Copy from `include/secrets.example.h`. Never commit it, and don't print its contents (WiFi password). The build fails with a missing `secrets.h` error without it.
- **`fix_tft.py` is load-bearing.** It is a PlatformIO pre-build script (`extra_scripts` in `platformio.ini`) that patches the downloaded TFT_eSPI. The library sets `SPI_PORT` to `SPI2_HOST` (=1), but this core's `REG_SPI_BASE(i)` only maps `i==2` to the SPI2 base, so the base is 0. The result is a store-access fault (`MTVAL 0x10`) in `tft.init()` and a boot loop. The script rewrites it to `2`. Don't remove it unless the platform or core is upgraded and verified.
- **Don't use `pio device monitor` from an agent shell.** It needs an interactive TTY. To read serial output, use pyserial from the PlatformIO venv at `~/.platformio/penv/bin/python` on `/dev/cu.usbmodem*` at 115200. Toggle RTS to reset the board and catch boot logs. Have the user run the monitor in their own terminal if they want it live.
- **A serial port can only have one user.** A lingering monitor makes `upload` fail with `Invalid head of packet` or serial-disconnect errors. Check `lsof /dev/cu.usbmodem*` first. Don't kill a process you didn't start. Ask the user.
- **Boot loops make USB flaky.** If upload can't connect, have the user hold BOOT, tap RST, release BOOT (download mode), then retry.
- **Decoding crashes:** `~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-addr2line -pfiaC -e .pio/build/lolin_c3_mini/firmware.elf <addr...>` on the `MEPC`, `RA` and stack addresses from a Guru Meditation dump.
- Strapping pins GPIO 2/8/9 should stay unused. The C3 has no TFT_eSPI DMA support, and the build prints `#warning`s for that and for `TOUCH_CS`. Both are harmless.

## Conventions

- Display pins and SPI settings are `-D` build flags in `platformio.ini`, not edits to the library's `User_Setup.h`.
- The 2.4 GHz WiFi band only (C3 has no 5 GHz). Timezone is a POSIX TZ string in `secrets.h`.
- Flashing hardware and installing system software are user-confirmed actions. Ask before doing either unless they've just asked for it.
