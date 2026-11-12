#pragma once
#include <math.h>
#include <time.h>
#include "config.h"

// Tide predictions from NOAA CO-OPS (api.tidesandcurrents.noaa.gov, no key): the high and low tides of a
// station, heights above MLLW (the tide-table datum). The station is the setup page's `tidestn`, or else the
// nearest one to the ZIP's coordinates (found once by streaming NOAA's station list; cached in NVS).
struct TideData {
  static constexpr int MAX = 16;  // 72 h of highs and lows is ~11-13 turns
  bool valid = false;             // turns hold data
  int n = 0;
  time_t t[MAX] = {};             // epoch seconds of each turn, ascending
  float h[MAX] = {};              // height (ft for 'F', m for 'C')
  bool high[MAX] = {};
  char unit = 'F';
  char error[24] = "";            // why there is no data ("NO TIDE STATION NEARBY"...); "" while loading
  char station[32] = "";          // short station name for the face ("CHARLESTON")
  float water = NAN;              // water temperature (°F for 'F', °C for 'C'), NAN if unknown
  uint32_t version = 0;           // bumps on every change
};

void tide_begin();                       // creates the lock (weather_begin calls it)
void tide_refresh(const ClockConfig &cfg);  // from the weather task: fetches when due (every 6 h, or settings changed)
TideData tide_get();                     // thread-safe snapshot
