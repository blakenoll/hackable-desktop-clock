---
name: ship
description: Verify the current changes with parallel eval subagents and, only if all pass, open a pull request. Use when the user says "ship it", "open a PR", or asks to evaluate and PR the changes.
---

# Evaluate, then open a PR

Never flash the board. Never print or commit credentials, `include/secrets.h`, or the street address.

## 1. Evaluate
Run `sh docs/tools/verify.sh` first (the gate; stop on failure and fix). Then launch these subagents **in one message, in parallel**:
- `build-eval` always (it skips `pio run` itself unless `src/`, `include/`, `platformio.ini`, `fix_tft.py`, `git_version.py` or `lib/` changed)
- `render-eval` if `src/ui/` or `src/data/weather_format.cpp` changed
- `rules-eval` always
- `ha-eval` if `src/api/`, `custom_components/` or `tests/home_assistant/` changed

Any `FAIL`: fix the cause, rerun only the failing evals, and repeat. Stop after 3 rounds and report what still fails; do not open a PR.

## 2. Branch
- Check `gh pr view --json state` for the current branch. If its PR is `MERGED` or closed, or the branch is `main`, create a new branch from the latest `origin/main` (`git fetch origin`) and carry over only the commits not in main.
- Stage files by name (never `git add -A`; skip `.claude/worktrees/` and `include/secrets.h`). Include re-rendered `docs/faces/` PNGs and `custom_components/deskclock/brand/` when the look changed.

## 3. PR
Commit with a clear message, `git push -u origin <branch>`, then `gh pr create` with: what changed and why, the eval results (one line each), and for UI changes the changed `docs/faces/*.png` paths. Return the PR URL.

If everything passed but the change is large or ambiguous, say so and ask before pushing.
