#pragma once

// The BOOT button (GPIO 9). Poll often. Reports BTN_SHORT once when a brief press is released,
// BTN_DOUBLE once for two brief presses in quick succession (BTN_SHORT is reported only after the
// double-press window passes), BTN_LONG once when it has been held for ~3 s. (Holding BOOT *during
// reset* enters the ROM bootloader, so the press has to happen while the firmware is running.)
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_DOUBLE, BTN_LONG };
ButtonEvent button_event();
// Same poll, when only the long press matters (short presses are dropped).
bool button_long_press();
