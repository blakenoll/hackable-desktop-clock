#!/bin/sh
# Stop hook: a quick (seconds) docs-consistency check when code, docs or skills changed.
# Blocks the stop (exit 2) on failure so the agent fixes it. The slow checks (build, renders,
# tests) are not run here; `sh docs/tools/verify.sh` and the /ship skill run them.
cd "$(dirname "$0")/../.." || exit 0
input=$(cat)
# Don't loop if we already blocked once this turn.
echo "$input" | grep -q '"stop_hook_active": *true' && exit 0
git diff --name-only HEAD | grep -qE '^(src/|include/|docs/|custom_components/|\.claude/|CLAUDE\.md|README\.md)' || exit 0
out=$(python3 docs/tools/check.py 2>&1) && exit 0
echo "docs/tools/check.py failed; fix before finishing:" >&2
echo "$out" | tail -40 >&2
exit 2
