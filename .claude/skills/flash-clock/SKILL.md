---
name: flash-clock
description: Build, flash and read serial output for the ESP32-C3 desktop clock with PlatformIO, avoiding the known pitfalls (Rosetta, busy serial port, flaky USB auto-reset, non-interactive monitor). Use whenever the user asks to build, upload, flash, test on the board, or see device logs.
---

# Build, flash and read logs

Read `CLAUDE.md` and `docs/TROUBLESHOOTING.md` for background. The short version:

## Rules

- **Flashing is the user's call.** Build freely; ask before flashing unless they just asked you to. Never install system software (e.g. Rosetta) yourself; tell the user the command and let them run it (`! <cmd>` in the prompt).
- Don't kill processes you didn't start (a lingering `pio device monitor` holds the port). Ask.
- You cannot see the display. After a UI change, say what to look for and ask the user how it looks.

## Build

```
mise run build   # if mise.toml exists: mise provides pio (also `mise run upload`)
```

Without mise:

```
export PATH="$PATH:$HOME/.platformio/penv/bin"   # pio may not be on PATH
pio run
```

- `Bad CPU type in executable` on Apple Silicon = Rosetta 2 missing → user runs `softwareupdate --install-rosetta --agree-to-license`.
- Report flash use from the summary line; the budget is the 3 MB `huge_app` partition (firmware ~1.39 MB).

## Flash

1. `lsof /dev/cu.usbmodem*` should print nothing (nobody else on the port).
2. `pio run -t upload`. Success ends with `Hash of data verified` and `[SUCCESS]`; the board resets itself.
3. If it fails with `No serial data received` (intermittent; the USB auto-reset is flaky), retry once. Then use the manual recipe: start a background retry loop with `PLATFORMIO_UPLOAD_FLAGS="--before=no_reset" pio run -t upload` (repeat until `[SUCCESS]`), and have the user unplug USB, **hold BOOT**, plug in while holding, and keep holding ~15 s. Tapping RST alone often doesn't enter download mode. Confirm with a passive serial read that the board is not just booting the app normally (`boot:0x0` / download mode vs. `SPI_FAST_FLASH_BOOT`).
4. After flashing a normal build, WiFi/ZIP/units are kept (NVS survives).

## Read logs

`pio device monitor` needs an interactive terminal and **will not work from here**. Use pyserial from PlatformIO's own venv instead, with the reset toggled so you catch the boot log:

```
~/.platformio/penv/bin/python -I -c '
import serial,sys,time
s=serial.Serial(sys.argv[1],115200,timeout=1)
s.dtr=False; s.rts=True; time.sleep(0.1); s.rts=False
end=time.time()+15
while time.time()<end:
    l=s.readline().decode(errors="replace").rstrip()
    if l: print(l)
' /dev/cu.usbmodem101
```

(Find the port with `ls /dev/cu.usbmodem*`.) Resetting the board regenerates the one-time setup-AP password, so read it from the same capture. The app logs very little in normal operation; a quiet log without `Guru Meditation` / `Backtrace` means it booted fine.

## Decode a crash

For a `Guru Meditation Error` dump, resolve the `MEPC`, `RA` and stack addresses:

```
~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-addr2line -pfiaC -e .pio/build/lolin_c3_mini/firmware.elf <addr> ...
```

`Store access fault` with `MTVAL 0x10` inside `TFT_eSPI::init` is the SPI-port bug that `fix_tft.py` patches; check that script is still wired in `platformio.ini`.

## Previewing night mode

Night mode normally follows sunset. To see it now: `PLATFORMIO_BUILD_FLAGS="-DFORCE_NIGHT=1" pio run -t upload`. **Afterwards flash a normal build** (without that variable) so the clock follows the real state again.
