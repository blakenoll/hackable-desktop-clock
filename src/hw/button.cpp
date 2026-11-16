// BOOT button (GPIO 9) as a three-gesture input: short, double and long press (timings in tuning.h).
#include "button.h"
#include <Arduino.h>
#include "../tuning.h"

static constexpr int BOOT_PIN = 9;  // the C3 SuperMini's BOOT button (a strapping pin: read only after boot)

ButtonEvent button_event() {
  static bool inited = false, down = false, fired = false, pending = false;
  static uint32_t since = 0, released = 0;
  if (!inited) {
    pinMode(BOOT_PIN, INPUT_PULLUP);
    inited = true;
  }
  if (digitalRead(BOOT_PIN) == LOW) {
    if (!down) {
      down = true;
      fired = false;
      since = millis();
      if (pending && since - released > BUTTON_DOUBLE_MS) pending = false;
    } else if (!fired && millis() - since >= BUTTON_LONG_PRESS_MS) {
      fired = true;  // once per hold
      return BTN_LONG;
    }
  } else if (down) {
    down = false;
    uint32_t held = millis() - since;
    if (!fired && held >= BUTTON_SHORT_MIN_MS && held <= BUTTON_SHORT_MAX_MS) {
      if (pending) {
        pending = false;
        return BTN_DOUBLE;
      }
      pending = true;
      released = millis();
    }
  } else if (pending && millis() - released > BUTTON_DOUBLE_MS) {
    pending = false;  // no second press: it was a single
    return BTN_SHORT;
  }
  return BTN_NONE;
}

bool button_long_press() { return button_event() == BTN_LONG; }
