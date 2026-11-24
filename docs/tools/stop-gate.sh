#!/bin/sh
# Stop hook: when firmware or docs changed, run the checks; block the stop (exit 2) on failure so the agent fixes it.
cd "$(dirname "$0")/../.." || exit 0
input=$(cat)
# Don't loop if we already blocked once this turn.
echo "$input" | grep -q '"stop_hook_active": *true' && exit 0
git diff --name-only HEAD | grep -qE '^(src/|include/|docs/|custom_components/|platformio\.ini|fix_tft\.py|git_version\.py)' || exit 0
out=$(sh docs/tools/verify.sh 2>&1) && exit 0
echo "verify.sh failed; fix before finishing:" >&2
echo "$out" | tail -40 >&2
exit 2
