#!/bin/sh
# Renders the real clock faces on this computer: compiles src/ui/ (the UI code) against LVGL with a stub Arduino.h,
# draws every face into a 240x240 buffer and writes montages to docs/faces/*.png.
# Needs gcc/g++, python3 and one prior `pio run` (for .pio/libdeps/.../lvgl). Usage: sh docs/tools/sim/render.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
LV="${LVGL_DIR:-$ROOT/.pio/libdeps/lolin_c3_mini/lvgl}"  # LVGL_DIR: use another LVGL 9.6 checkout
[ -d "$LV" ] || { echo "LVGL not found: run 'pio run' once first"; exit 1; }
B="${TMPDIR:-/tmp}/clock-sim"
mkdir -p "$B/out" "$B/conf"
# Same lv_conf.h as the firmware, with a bigger heap (host pointers are twice the size).
sed 's/#define LV_MEM_SIZE .*/#define LV_MEM_SIZE (512 * 1024)/' "$ROOT/include/lv_conf.h" > "$B/conf/lv_conf.h"
# Compiled LVGL is cached per LVGL checkout + config, so changing either rebuilds it.
OBJ="$B/obj/$( (echo "$LV"; cat "$B/conf/lv_conf.h") | cksum | cut -d' ' -f1)"
mkdir -p "$OBJ"
CF="-O1 -w -DLV_CONF_INCLUDE_SIMPLE -I$B/conf -I$HERE/stub -I$LV -I$LV/src -I$ROOT/src"
find "$LV/src" -name '*.c' | while read -r f; do
  o="$OBJ/$(echo "$f" | cksum | cut -d' ' -f1).o"
  [ -f "$o" ] || echo "$f $o"
done | xargs -r -P 8 -n 2 sh -c 'gcc '"$CF"' -c "$0" -o "$1"'
for f in "$ROOT"/src/ui/fonts/*.c; do gcc $CF -c "$f" -o "$B/font_$(basename "$f" .c).o"; done
rm -f "$B"/ui_*.o
for f in "$ROOT"/src/ui/*.cpp "$ROOT"/src/ui/faces/*.cpp; do g++ $CF -std=gnu++17 -c "$f" -o "$B/ui_$(basename "$f" .cpp).o"; done
g++ $CF -std=gnu++17 -c "$HERE/sim.cpp" -o "$B/sim.o"
g++ -o "$B/sim" "$B/sim.o" "$B"/ui_*.o "$B"/font_*.o "$OBJ"/*.o -lm
"$B/sim" "$B/out"
cd "$B/out"
# Face names (enum order) and the theme / night-colour counts, written by sim.cpp from the UI tables.
N=$(sed -n 1p manifest.txt)
set -- $(sed -n 2p manifest.txt)
THEMES=$1 NIGHTS=$2
python3 "$HERE/montage.py" --bezel "$ROOT/docs/faces/faces.png" 4 $(for n in $N; do echo day-$n; done)
python3 "$HERE/montage.py" --bezel "$ROOT/docs/faces/night.png" 4 $(for n in $N; do echo night-$n; done)
python3 "$HERE/montage.py" --bezel "$ROOT/docs/faces/options.png" 4 $(for n in $N; do echo opt-$n; done)
python3 "$HERE/montage.py" --bezel "$ROOT/docs/faces/states.png" 3 $(for n in polished stacked forecast; do echo load-$n msg-$n stale-$n; done)
# Per-theme / per-night-colour montages for the web flasher's pickers (same 4-column layout as faces.png).
mkdir -p "$ROOT/docs/faces/themes"
for th in $(seq 0 $((THEMES - 1))); do
  python3 "$HERE/montage.py" "$ROOT/docs/faces/themes/day-$th.png" 4 $(for n in $N; do echo theme$th-$n; done)
done
for nc in $(seq 0 $((NIGHTS - 1))); do
  python3 "$HERE/montage.py" "$ROOT/docs/faces/themes/night-$nc.png" 4 $(for n in $N; do echo nightc$nc-$n; done)
done
echo "wrote docs/faces/*.png and docs/faces/themes/*.png"
