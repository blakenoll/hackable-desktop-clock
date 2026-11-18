#pragma once
// Timezone presets (zones.cpp): offered on the setup page and listed by the settings API's schema (src/api/).

struct Zone {
  const char *label, *posix;
};
extern const Zone ZONES[];
extern const int ZONE_COUNT;
