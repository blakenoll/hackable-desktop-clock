#!/bin/sh
# Renders the real clock faces on this computer: compiles src/ui.cpp against LVGL with a stub Arduino.h,
# draws every face into a 240x240 buffer and writes montages to docs/faces/*.png.
# Needs gcc/g++, python3 and one prior `pio run` (for .pio/libdeps/.../lvgl). Usage: sh docs/tools/sim/render.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
LV="$ROOT/.pio/libdeps/lolin_c3_mini/lvgl"
[ -d "$LV" ] || { echo "LVGL not found: run 'pio run' once first"; exit 1; }
B="${TMPDIR:-/tmp}/clock-sim"
mkdir -p "$B/obj" "$B/out" "$B/conf"
# Same lv_conf.h as the firmware, with a bigger heap (host pointers are twice the size).
sed 's/#define LV_MEM_SIZE .*/#define LV_MEM_SIZE (512 * 1024)/' "$ROOT/include/lv_conf.h" > "$B/conf/lv_conf.h"
CF="-O1 -w -DLV_CONF_INCLUDE_SIMPLE -I$B/conf -I$HERE/stub -I$LV -I$LV/src -I$ROOT/src"
find "$LV/src" -name '*.c' | while read -r f; do
  o="$B/obj/$(echo "$f" | cksum | cut -d' ' -f1).o"
  [ -f "$o" ] || echo "$f $o"
done | xargs -P 8 -n 2 sh -c 'gcc '"$CF"' -c "$0" -o "$1"'
for f in "$ROOT"/src/font_*.c; do gcc $CF -c "$f" -o "$B/font_$(basename "$f" .c).o"; done
g++ $CF -std=gnu++17 -c "$ROOT/src/ui.cpp" -o "$B/ui.o"
g++ $CF -std=gnu++17 -c "$HERE/sim.cpp" -o "$B/sim.o"
g++ -o "$B/sim" "$B/sim.o" "$B/ui.o" "$B"/font_*.o "$B"/obj/*.o -lm
"$B/sim" "$B/out"
N="polished arc stacked analog zen forecast retro daylight"
cd "$B/out"
python3 "$HERE/montage.py" "$ROOT/docs/faces/faces.png" 4 $(for n in $N; do echo day-$n; done)
python3 "$HERE/montage.py" "$ROOT/docs/faces/night.png" 4 $(for n in $N; do echo night-$n; done)
python3 "$HERE/montage.py" "$ROOT/docs/faces/options.png" 4 $(for n in $N; do echo opt-$n; done)
python3 "$HERE/montage.py" "$ROOT/docs/faces/states.png" 3 load-polished msg-polished stale-polished load-stacked msg-stacked stale-stacked load-forecast msg-forecast stale-forecast
echo "wrote docs/faces/*.png"
