# Troubleshooting

## Building

**`Bad CPU type in executable` (Apple Silicon Mac).** The pinned toolchain is x86_64-only. Install Rosetta 2: `softwareupdate --install-rosetta --agree-to-license`, then rebuild.

**`pio: command not found`.** PlatformIO's CLI lives in `~/.platformio/penv/bin`. Add it to your PATH (`export PATH="$PATH:$HOME/.platformio/penv/bin"` in your shell profile) or use the full path.

**The first build takes minutes.** It downloads the toolchain, Arduino core and libraries. Later builds take seconds to a minute.

## Flashing

**`No serial data received` / `Failed to connect to ESP32-C3`.** The C3's USB auto-reset is occasionally unreliable (cause not pinned down). Try, in order:

1. Run the upload again; unplug and replug the cable first.
2. Try a different cable or port (some are charge-only).
3. Put the chip in download mode by hand: unplug USB, hold **BOOT**, plug in while holding, and keep holding while you run the upload with the reset step disabled:
   ```
   PLATFORMIO_UPLOAD_FLAGS="--before=no_reset" pio run -t upload
   ```
   Release BOOT only once the upload shows progress. Tapping RST alone often doesn't enter download mode on this board.

**`Invalid head of packet` or a port-busy error.** Something else holds the serial port, usually a running `pio device monitor`. Close it, or check with `lsof /dev/cu.usbmodem*`.

## The display

**Boot loop with `Guru Meditation Error ... Store access fault` right after power-up.** That is the TFT_eSPI / ESP32-C3 SPI-port bug that `fix_tft.py` patches. Check that `extra_scripts = pre:fix_tft.py` is still in `platformio.ini` and that the build prints `fix_tft.py: patched SPI_PORT ...` (it prints once, the first time after the library is installed). See [HARDWARE.md](HARDWARE.md).

**Blank screen.** Check wiring against the table in the README, especially RST/DC/CS. If your module has a BLK pin, enable the backlight flags in `platformio.ini`. If the serial log looks healthy, the pin mapping is the usual cause.

**Garbled picture.** Lower `SPI_FREQUENCY` (try `27000000`), shorten jumper wires, check power.

**Wrong or inverted colors.** See the TFT_eSPI color-order/inversion flags in [HARDWARE.md](HARDWARE.md). This wasn't needed on the tested module.

## Setup and WiFi

**The setup page didn't pop up.** Some phones don't auto-open captive portals. Browse to `http://192.168.4.1` manually. If your phone says "no internet" and offers to leave the network, choose to stay connected.

**I don't know the setup password.** It's random each time setup starts and is shown on the screen (and in the QR code). It's also printed on the serial console at 115200 baud.

**My network isn't listed.** The clock scans once when setup starts. You can type the network name by hand. It must be 2.4 GHz (the ESP32-C3 has no 5 GHz radio).

**It keeps going back to the setup screen.** It couldn't join the saved network within 20 seconds (wrong password, out of range, 5 GHz-only network). Re-enter the details. If the screen is just left alone for 5 minutes, it reboots and retries the saved network.

**Re-open setup later.** Hold the **BOOT button for 3 seconds** while the clock is running.

**Wipe all saved settings.** `pio run -t erase`, then flash again.

## Time

**Wrong time or timezone.** Pick the timezone on the setup page, or type a POSIX TZ string (for example `EST5EDT,M3.2.0,M11.1.0`) in the custom field. See the [POSIX TZ list](https://github.com/nayarsystems/posix_tz_db).

**Stuck on "Syncing time...".** NTP (`pool.ntp.org`, `time.nist.gov`) is unreachable from your network, or UDP port 123 is blocked.

## Weather and night mode

**`ZIP not found`.** Only US 5-digit ZIP codes are supported. Double-check the digits.

**`No weather`.** The weather request failed and there's nothing cached yet (WiFi/internet problem, or Open-Meteo unreachable). It retries every 60 seconds. After the first success, a later failure keeps showing the last reading.

**The weather skeleton pulses forever.** WiFi dropped before the first fetch finished, or the request is hanging. It resolves to data or an error message once a request completes.

**Weather is blank and there's no night mode.** No ZIP code was entered. Re-open setup and add one. Night mode depends on the weather fetch.

**Night mode comes on a bit late or early.** It follows Open-Meteo's `is_day` flag, which is checked every 15 minutes.

**There's no degree sign (`°`).** The built-in fonts have no degree sign, so temperatures read `72F`. See [CUSTOMIZING.md](CUSTOMIZING.md) to add one.

## Reading logs

The serial console runs at 115200 baud over the board's USB port. Run `pio device monitor` in a normal terminal (it needs an interactive terminal; it won't work piped or from some tool shells). On boot, setup mode prints the access-point name and password.
