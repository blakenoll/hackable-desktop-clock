#include "button.h"
#include <Arduino.h>

static constexpr int BOOT_PIN = 9;
static constexpr uint32_t LONG_PRESS_MS = 3000;
static constexpr uint32_t SHORT_MIN_MS = 40, SHORT_MAX_MS = 1000;  // debounce .. clearly not a hold
static constexpr uint32_t DOUBLE_MS = 350;  // second press must start within this of the first release

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
      if (pending && since - released > DOUBLE_MS) pending = false;
    } else if (!fired && millis() - since >= LONG_PRESS_MS) {
      fired = true;  // once per hold
      return BTN_LONG;
    }
  } else if (down) {
    down = false;
    uint32_t held = millis() - since;
    if (!fired && held >= SHORT_MIN_MS && held <= SHORT_MAX_MS) {
      if (pending) {
        pending = false;
        return BTN_DOUBLE;
      }
      pending = true;
      released = millis();
    }
  } else if (pending && millis() - released > DOUBLE_MS) {
    pending = false;  // no second press: it was a single
    return BTN_SHORT;
  }
  return BTN_NONE;
}

bool button_long_press() { return button_event() == BTN_LONG; }
