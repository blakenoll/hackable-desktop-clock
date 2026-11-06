---
name: change-hardware
description: Adapt the ESP32 desktop clock firmware to different hardware - another display controller or size, different pin wiring, a backlight pin, or a different ESP32 board. Use when the user's parts or wiring differ from the Lolin C3 Mini + GC9A01 240x240 round display.
---

# Adapting to different hardware

Everything hardware-specific is in `platformio.ini` build flags plus a few constants; `docs/HARDWARE.md` is the reference. Only the **ESP32-C3 + GC9A01 round 240x240** combination has been tested; be upfront about that.

## First, find out

Ask (or read from what the user said): board/chip, flash size, display controller and resolution, round or square, SPI pin wiring, whether the module has a backlight (BLK) pin, and where the BOOT button is.

## Checklist

1. **Pins** — `-DTFT_MOSI/SCLK/CS/DC/RST` (and optionally `-DTFT_BL` + `-DTFT_BACKLIGHT_ON`) in `platformio.ini`. Avoid strapping, USB and flash pins for the chip in use.
2. **Display driver and size** — replace `-DGC9A01_DRIVER=1` with the TFT_eSPI define for the controller; set `TFT_WIDTH`/`TFT_HEIGHT`. Color order/inversion flags if colors are wrong.
3. **Resolution in code** — `W`, `H` and `drawBuf` in `src/main.cpp`.
4. **UI geometry** — `src/ui.cpp`: `RING_CENTER`, `TICK_OUTER`, tick lengths, the ±58 label offsets, weather row width. The design assumes a 240x240 round screen; on square screens the corners are free to use, on other sizes everything needs rescaling.
5. **Board** — `board =` in `platformio.ini`. Under 4 MB flash can't use `huge_app.csv`; the firmware is ~1.39 MB, so something must be trimmed (font subset, icons, features) or a different partition scheme chosen. Remove `-DARDUINO_USB_CDC_ON_BOOT=1` for boards with a UART bridge.
6. **BOOT button** — `BOOT_PIN` in `src/provisioning.cpp`.
7. **`fix_tft.py`** only affects the ESP32-C3 header; it is a no-op on other chips. Don't change the pinned platform without checking `docs/HARDWARE.md`.

## Then

- `pio run`; fix compile errors (other chips may need different TFT_eSPI processor support, which can fail in the library itself).
- Ask the user before flashing (`flash-clock` skill). You can't see the display; ask them to describe it (blank, garbled, inverted colors, offset) and map symptoms with `docs/TROUBLESHOOTING.md`.
- Record what you changed and what you verified in `docs/HARDWARE.md` so the next person knows it's been tried.
