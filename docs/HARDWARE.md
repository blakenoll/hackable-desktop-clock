# Hardware notes

Tested on: **Lolin (Wemos) C3 Mini** (ESP32-C3, 4 MB flash) + a **1.28" GC9A01 240x240 round SPI display**, on macOS (Apple Silicon). Everything below that goes beyond that combination is guidance, not something that has been tried.

## Where the hardware is configured

All in `platformio.ini`:

| Setting | Meaning |
|---------|---------|
| `board = lolin_c3_mini` | Board definition |
| `-DGC9A01_DRIVER=1`, `-DTFT_WIDTH=240`, `-DTFT_HEIGHT=240` | Display driver and size ([TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) options) |
| `-DTFT_MOSI`, `-DTFT_SCLK`, `-DTFT_CS`, `-DTFT_DC`, `-DTFT_RST` | SPI pins (display SDA = MOSI, SCL = SCLK) |
| `-DTFT_BL` / `-DTFT_BACKLIGHT_ON` | Optional backlight pin (commented out by default) |
| `-DSPI_FREQUENCY=40000000` | SPI clock; lower it (e.g. `27000000`) if the picture is noisy |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | Route `Serial` over the C3's built-in USB |
| `board_build.partitions = huge_app.csv` | ~3 MB app partition, no OTA slot. Needs a 4 MB flash chip. |

The BOOT-button pin used for re-entering setup is `BOOT_PIN` (GPIO 9) in `src/provisioning.cpp`.

## Different wiring (same board and display)

Change the `-DTFT_*` pin flags and rewire to match. On the C3, avoid GPIO 2, 8 and 9 (strapping pins), GPIO 18/19 (USB), and the pins wired to the flash chip; check your board's pinout. The SPI signals can go to most other GPIOs.

## A different display

1. In `platformio.ini`, replace `-DGC9A01_DRIVER=1` with your controller's TFT_eSPI define (`ST7789_DRIVER`, `ILI9341_DRIVER`, …) and set `TFT_WIDTH` / `TFT_HEIGHT`. See TFT_eSPI's `User_Setup.h` for the list and for options such as color order and inversion (`TFT_RGB_ORDER`, `TFT_INVERSION_ON`) if colors look wrong. These were not needed on the tested module.
2. In `src/main.cpp`, update `W` and `H` and the `drawBuf` size (a 40-line partial render buffer).
3. **The UI assumes a 240x240 round screen.** For other sizes or square panels, adjust in `src/ui.cpp`: `RING_CENTER`, `TICK_OUTER` and the tick lengths (or remove the ring), the ±58 px label offsets, and the weather row width (190). On a square screen you can use the corners that the round bezel hides.
4. Other color depths are not supported: LVGL runs in RGB565 and `flushCb` byte-swaps for the SPI bus.

## A different ESP32 board

- Change `board =` and the pin flags. Boards with fewer than 4 MB of flash can't use `huge_app.csv`; the firmware is ~1.39 MB, which is larger than the default 1.3 MB app slot, so you would have to trim it (subset the 48 pt font, disable unused LVGL features, drop the weather icons) or pick a smaller partition scheme.
- `-DARDUINO_USB_CDC_ON_BOOT=1` is for chips with native USB (C3, S3). Remove it for boards with a UART bridge chip.
- The BOOT button is usually GPIO 0 on classic ESP32/S3 boards; change `BOOT_PIN`.
- `fix_tft.py` only does anything for the ESP32-C3 header; on other chips it silently does nothing.
- Only the ESP32-C3 has been tested.

## Why the platform and libraries are pinned

`platformio.ini` pins `espressif32@6.10.0`, TFT_eSPI 2.5.43 and LVGL 9.6.0. This is deliberate:

- The 6.10.0 platform uses Arduino core 2.x and an x86_64-only `toolchain-riscv32-esp@8.4.0`. On Apple Silicon that needs **Rosetta 2**. Newer toolchains (13.2+) have native arm64 builds but come with Arduino core 3.x (for example via the community *pioarduino* platform). **Switching to them has not been tried**; TFT_eSPI's support for core 3.x is the likely trouble spot.
- **`fix_tft.py` exists because of core 2.x.** TFT_eSPI's C3 header defines `SPI_PORT` as `SPI2_HOST` (enum value 1), but this core's `REG_SPI_BASE(i)` only maps `i == 2` to the SPI2 register base. The result is a null base address and a store-access fault in `tft.init()` — a boot loop. The script rewrites the define to `2` before each build. If you move to a newer core, remove the script only after checking the display still initializes.
- Bumping LVGL: the UI uses 9.x APIs (`lv_spinner_create(parent)`, `lv_obj_remove_flag`, `lv_line` with `lv_point_precise_t`). Rebuild and check every screen after a bump.

## Flash and memory

- Firmware is ~1.39 MB of the 3 MB partition; the weather/TLS code alone is ~158 KB.
- The 48 pt Manrope time font is already subset to `0-9` and `:`.
- RAM use is ~41%. `LV_MEM_SIZE` is 64 KB in `include/lv_conf.h`.
- Saved settings (WiFi, timezone, ZIP, units, last day/night state) live in the NVS partition at `0x9000`, which the `huge_app` table keeps in the same place as the default table, so they survive a reflash. `pio run -t erase` wipes them.
