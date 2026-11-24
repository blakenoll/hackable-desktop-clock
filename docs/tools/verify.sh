#!/bin/sh
# Run the same checks CI runs, plus the face renders. Exits non-zero on the first failure.
# Usage: sh docs/tools/verify.sh [--base <ref>]   (base defaults to origin/main; used to decide which checks apply)
set -e
cd "$(dirname "$0")/../.."
export PATH="$PATH:$HOME/.platformio/penv/bin"

BASE=origin/main
[ "$1" = "--base" ] && BASE=$2
CHANGED=$( (git diff --name-only "$BASE"...HEAD; git diff --name-only HEAD; git ls-files --others --exclude-standard) 2>/dev/null | sort -u)

step() { printf '\n== %s ==\n' "$1"; }

step "docs consistency"
python3 docs/tools/check.py

if echo "$CHANGED" | grep -v '^include/secrets\.h$' | grep -qE '^(src/|include/|platformio\.ini|fix_tft\.py|git_version\.py|lib/)'; then
  step "firmware build"
  pio run
else
  step "firmware build (skipped: no compiled files changed)"
fi

if echo "$CHANGED" | grep -qE '^(custom_components/|tests/home_assistant/|src/api/)'; then
  step "Home Assistant tests"
  python3 -m pytest tests/home_assistant -q
else
  step "Home Assistant tests (skipped: no API or integration changes)"
fi

if echo "$CHANGED" | grep -qE '^src/ui/|^src/data/weather_format'; then
  step "face renders"
  sh docs/tools/sim/render.sh
  echo "Re-rendered PNGs (commit these if the look changed):"
  git status --porcelain docs/faces custom_components/deskclock/brand
else
  step "face renders (skipped: no UI changes)"
fi

printf '\nVERIFY PASSED\n'
