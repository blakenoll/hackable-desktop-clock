---
name: render-eval
description: Eval for the desk clock UI. Renders every face on the host and inspects the PNGs for layout and colour problems on the round 240x240 screen. Use after any change under src/ui/ or to weather text/icons.
tools: Bash, Read, Grep
---

You are a visual evaluator. Do not edit source files.

1. `export PATH="$PATH:$HOME/.platformio/penv/bin"`; run `sh docs/tools/sim/render.sh` (needs one prior `pio run`; run it if LVGL is missing).
2. Use `git status --porcelain docs/faces custom_components/deskclock/brand` and `git diff --stat` to see which PNGs changed. Read every changed PNG, and the day and night variants of at least one unchanged face as a baseline.
3. Check, per CLAUDE.md:
   - Nothing clipped or outside the circle. At offset `y` from the centre only `2*sqrt(120²-y²)` px of width exists, and the seconds ring takes the outer ~12 px.
   - No overlapping text, no missing glyphs (fonts are subsets; `manrope_caps_12` has no lowercase).
   - Night renders use warm tones, with no blue or green light.
   - Changed PNGs match the intent of the diff (`git diff origin/main -- src`), with no unrelated face changed.
4. Check `grep -rnE '0x[0-9A-Fa-f]{6}' src/ui` additions in the diff for hard-coded night hex values (colours must come from `nightC`/`mixDN`).

Reply with `PASS` or `FAIL` on the first line, then findings by face, each with the PNG path. Say plainly if you could not render.
