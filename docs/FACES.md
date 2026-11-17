# Clock faces

Ten faces, in `enum Face` order (`src/ui/ui.h`): Polished (default), Arc, Stacked, Analog, Zen, Forecast, Retro, Daylight, Tide, Radar. A short press of BOOT cycles them; the setup page lists them from the face table. Renders of all of them are in `docs/faces/faces.png` (from `sh docs/tools/sim/render.sh`).

## How a face is defined

Each face is one file, `src/ui/faces/face_<name>.cpp`, that defines a `FaceDef` (declared in `src/ui/ui_internal.h`) plus whatever private state it needs. `src/ui/faces.cpp` lists them in enum order.

| `FaceDef` member | What it does |
|---|---|
| `name` | Shown on the setup page; its first word, lowercased, names the render files |
| `ring` | Ring kind, see below |
| `build()` | Creates the face's objects, usually from shared pieces: `mkTime`, `mkDateRow`, `mkLabelAt` (`time_view.cpp`), `mkWeather`, `mkDetail`, `mkOffline` (`weather_view.cpp`). Coordinates are pixel offsets from the centre |
| `date(d)` | Lays out the date; default is weekday + date labels. `dateQuietLine` gives "TUE · NOV 10" |
| `palette(p, T)` | Overrides default text colours |
| `paint(T, p)` / `paintRing(T)` | Recolour the face's own objects / custom ring (called on every theme or night step) |
| `onTime(t, minuteChanged)` | Every `ui_update()`, after the time text |
| `onData()` | Hourly, sun or wind data changed, or the face was rebuilt |
| `onText(label)` | A label's text was just set |
| `onStale(opa, ms)` | Dim face-private weather objects |
| `detailPrefix()` | Text shown before the detail line ("NW 12 MPH") |
| `detailOwn`, `msgInDetail`, `fullDate`, `ringAtNight` | Flags: the prefix is the whole detail line; weather messages without an icon go on the detail line; long day and month names; ring stays (dimmed) at night |

### Rules

- **Shared code never branches on `opt.face`.** If a face needs different behaviour, add a hook or flag to `FaceDef`.
- **Pointers may be null.** A face creates only what it uses. Shared code goes through `tc`, `bgc`, `setText`, `setHidden`, `swapText` or checks first.
- **A face's private statics are stale while another face is active.** Shared code reaches them only through the hooks. **Why:** calling a face's helper directly from shared code crashed once.
- **Stop what outlives the objects.** Anything that animates a non-object variable or uses an `lv_timer` (the chime, `rangeFrac`, `orbitA`, the radar sweep) must be stopped from `rebuild()` (`ringStop()`, `weatherReset()`), or deleted with an object the face owns.
- **Night parity.** Every colour the face sets needs a night value (`mixDN(day, nightC(pct))`).

### Adding a face

1. Copy a small face such as `face_zen.cpp` to `face_<name>.cpp` and rename its `FaceDef` (`faceDef<Name>`).
2. Add `FACE_<NAME>` before `FACE_COUNT` in `enum Face` (`src/ui/ui.h`).
3. Add `&faceDef<Name>` at the same position in `FACES[]` (`src/ui/faces.cpp`), and its `extern` declaration above it.
4. Run `sh docs/tools/sim/render.sh` and look at `docs/faces/faces.png`. The renderer picks the new face up automatically. Then `python3 docs/tools/check.py`.
5. Update the face list in `README.md` and in this file.

## Ring kinds (`RingKind`, `src/ui/ring.cpp`)

| Kind | Look | Used by |
|---|---|---|
| `K_TICKS` | 60 line ticks with a comet tail | Stacked |
| `K_DOTTICKS` | same, minute ticks drawn as dots | Polished |
| `K_BLOCKS` | chunky LCD blocks that fill up | Retro |
| `K_MARKERS` | static analog batons | Analog |
| `K_ARC` | one `lv_arc` that grows with the seconds | Arc |
| `K_ORBIT` | a single dot on a hairline circle | Zen |
| `K_CUSTOM` | the face builds its own in `ringBox` | Forecast (compass), Daylight (24 h dial) |
| `K_NONE` | no ring | Tide, Radar |

- Geometry (radii, lengths, widths per kind) is in `tickGeo()`; colours are in `styleTick()`. Only changed ticks are restyled each second.
- **Comet tail:** the 12 ticks behind the current second (`TAIL_LEN`) fade from a soft blue-grey to a deliberately dim base, with a squared falloff. The tail wraps past 12 o'clock, and the previous minute's lit ticks dissolve over the first `TAIL_LEN` seconds, so nothing snaps at :00. **Why:** a white, 15-tick tail read too bright.
- The boot splash and hourly chime only run on the tick kinds.

## Face notes

**Polished (default):** date at y −62, time in the 56 pt font, weather row at +62 (icon + temperature + a vertical range bar: a gradient `rangeTrack` with `rangeDot` at the current temperature between today's low and high, numbers at the ends), and the detail line at +90. AM/PM is a separate label aligned to the right of the minutes (`placeAmpm()`), so it doesn't push the digits off-centre. The dot glides (`rangeFrac` animation) when the temperature changes. **Why a vertical bar:** a horizontal bar plus an arc at the bottom of the ring was tried and was too crowded on a round face.

**Forecast:** weather in the centre, small time on top, and a compass ring (`K_CUSTOM`) with a needle from the wind-from bearing (`ui_set_wind()`; hidden when calm). The detail line carries the wind readout ("NW 12 MPH · FEELS 65°", `detailPrefix`, `detailOwn`). Below that are hourly rain bars, or temperature bars with values when no hour reaches 30 %. There's no seconds ring, so its colon pulses each second (solid at night).

**Daylight:** a 24-hour dial with the sunrise-to-sunset arc and a sun dot at "now" (from `ui_set_weather_extra()` sunrise / sunset).

**Tide** (`face_tide.cpp`; data in [DATA.md](DATA.md#tides)): no seconds ring, so the chart is 216 px wide. A half-cosine runs between turns from −6 h to +12 h with fill columns; "now" is a ringed marker and a thin vertical line. Below the chart: height and trend, `LOW 3:58 PM · WATER 69°`, and the short station name (text before ` (` or `,`, uppercased).

**Radar** (`face_radar.cpp`; data in [DATA.md](DATA.md#radar)):
- **Scope:** radius 112 px = `RADAR_NM` (5 NM), four range rings, north up.
- **Sweep:** four wedge `lv_arc`s rotated by an `lv_anim` on `sweepBox`. Each arc's width is radius − 1. **Why:** at width ≥ radius, LVGL's `lv_draw_arc_get_area()` invalidates the whole arc box and redrew the full scope every frame.
- **Blips:** 29 px boxes (a dot plus a 12 px, 2 px rounded track leader), so moving one invalidates little. The box is moved before the leader is updated, so the leader's redraw falls inside the box's.
- **Motion:** blips dead-reckon every second from ground speed and track (capped at 60 s). Each blip's opacity follows how long ago the beam passed it (floor 100/255).
- **Tags:** callsign tags for the nearest three are placed by `placeTags()` (four candidate spots, inside the bezel, clear of other tags and dots).
- **Night:** the sweep stops and hides, and blips stay steady.
- **Cost:** about 20k redrawn px per frame (was 50.6k).

**Retro:** seven-segment digits built from boxes, with a block ring. **Analog:** hands, a date window at 3 o'clock and the weather at 6; the batons stay dim at night so the hands stay readable, and the second hand hides. **Stacked:** hours over minutes in the 74 pt font with fixed-width digits so the columns line up, the date on the left and the weather on the right. **Arc:** a sweeping seconds arc and a two-line full date. **Zen:** light 64 pt digits and an orbiting dot.

## Checking a face without flashing

`sh docs/tools/sim/render.sh` compiles `src/ui/` against LVGL with a stub `Arduino.h` and writes, to `docs/faces/`:

- `faces.png` (day), `night.png`, `options.png`, `states.png` (loading / message / stale)
- `themes/day-N.png` (every face in `THEMES[N]`) and `themes/night-N.png` (`NIGHTS[N]`) for the web flasher's pickers

To stage another situation, add a section to `docs/tools/sim/sim.cpp`.
