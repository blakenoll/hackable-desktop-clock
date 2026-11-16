#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// One HTTPS session at a time across the background tasks (weather, tides, radar). A TLS handshake needs a
// ~35-40 KB contiguous heap block and two sessions don't fit: while the tide task streamed NOAA's station list,
// every radar handshake failed with "SSL - Memory allocation failed". Hold a NetLock for the whole session
// (begin .. end, including any streamed body). It is a FreeRTOS mutex, so a low-priority holder (radar) is
// boosted while a higher-priority task (weather) waits on it.
class NetLock {
 public:
  explicit NetLock(TickType_t wait = portMAX_DELAY) : held_(xSemaphoreTake(mutex(), wait) == pdTRUE) {}
  ~NetLock() {
    if (held_) xSemaphoreGive(mutex());
  }
  bool held() const { return held_; }
  NetLock(const NetLock &) = delete;
  NetLock &operator=(const NetLock &) = delete;

 private:
  static SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();  // thread-safe static init (C++11)
    return m;
  }
  bool held_;
};
