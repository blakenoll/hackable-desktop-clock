#pragma once
#include <stdint.h>
#include "../tuning.h"  // RADAR_NM, RADAR_POLL_MS

// Live aircraft around the ZIP's coordinates from adsb.fi's open data API (opendata.adsb.fi, no key; personal,
// non-commercial use, attribution to adsb.fi required; 1 request/s limit). Only polled while the Radar face is
// showing (aircraft_set_active), every RADAR_POLL_MS.

struct Aircraft {
  float x = 0, y = 0;     // NM east / north of home, at AircraftData::fetchedAt
  float gs = 0;           // ground speed, knots (0 if unknown)
  float track = 0;        // degrees clockwise from true north
  int alt = -1;           // barometric altitude, ft; -1 = unknown (aircraft on the ground are left out)
  char flight[9] = "";    // callsign ("UAL2609"), else the registration, else the hex code
  char type[5] = "";      // ICAO type ("B39M"), "" if unknown
};

struct AircraftData {
  static constexpr int MAX = 16;  // nearest ones kept
  bool valid = false;             // a fetch succeeded (n may be 0: nothing in range)
  bool loading = false;           // active and waiting for the first result
  int n = 0;                      // ac[0..n) sorted nearest first
  int total = 0;                  // airborne aircraft in range (may exceed MAX)
  Aircraft ac[MAX];
  uint32_t fetchedAt = 0;         // millis() the positions are for
  char error[24] = "";            // why there is no data ("SET A ZIP FOR RADAR"...)
  uint32_t version = 0;           // bumps on every change
};

// Starts / stops polling (the task is created on the first call with on = true).
void aircraft_set_active(bool on);
AircraftData aircraft_get();  // thread-safe snapshot
