#!/usr/bin/env python3
"""Consistency check for the code and the docs that describe it. Run from anywhere: python3 docs/tools/check.py

Checks that:
  - every repo path named in backticks in the docs / skills exists (globs like face_*.cpp must match something),
    and every relative Markdown link resolves
  - every function named in backticks as `name()` exists in the code (src/, site/, docs/tools/), except known
    library calls (LVGL, Arduino, FreeRTOS...)
  - the faces line up: `enum Face` (src/ui/ui.h), FACES[] (src/ui/faces.cpp) and src/ui/faces/face_*.cpp
  - the web flasher's PALETTES (site/index.html) match THEMES[] / NIGHTS[] in src/ui/theme.cpp
Exits non-zero with a list of problems. Uses only the standard library.
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
problems = []


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


DOCS = ["CLAUDE.md", "README.md"] + sorted(
    glob.glob("docs/*.md", root_dir=ROOT) + glob.glob(".claude/skills/*/SKILL.md", root_dir=ROOT)
    + glob.glob("src/**/README.md", root_dir=ROOT, recursive=True))

# ---- Paths -----------------------------------------------------------------------------------------
PATH_RE = re.compile(r"`((?:src|docs|include|site|\.claude|\.github)/[^`\s]*|[\w.-]+\.(?:ini|py|toml|md))`")
for doc in DOCS:
    text = read(doc)
    for m in PATH_RE.finditer(text):
        p = m.group(1).rstrip("/").split(":")[0]
        if "<" in p or "{" in p:  # placeholders such as face_<name>.cpp
            continue
        base = ROOT if "/" in p or os.path.exists(os.path.join(ROOT, p)) else os.path.join(ROOT, os.path.dirname(doc))
        if not glob.glob(os.path.join(base, p)):
            problems.append(f"{doc}: path `{p}` does not exist")

# Relative Markdown links: [text](../path#anchor)
LINK_RE = re.compile(r"\]\(([^)#\s]+)(?:#[^)]*)?\)")
for doc in DOCS:
    for m in LINK_RE.finditer(read(doc)):
        target = m.group(1)
        if re.match(r"[a-z]+:", target):  # http:, mailto:...
            continue
        if not os.path.exists(os.path.normpath(os.path.join(ROOT, os.path.dirname(doc), target))):
            problems.append(f"{doc}: link ({target}) does not exist")

# ---- Functions -------------------------------------------------------------------------------------
code = ""
for pattern in ("src/**/*.cpp", "src/**/*.h", "site/*.html", "docs/tools/**/*.cpp", "docs/tools/**/*.sh"):
    for f in glob.glob(pattern, root_dir=ROOT, recursive=True):
        code += read(f) + "\n"
LIBRARY = {  # library functions the docs mention
    "getLocalTime", "configTzTime", "delay", "millis", "requestPort", "flash", "writeToStream", "setInsecure",
    "scanNetworks", "strftime", "snprintf", "xTaskCreate", "REG_SPI_BASE",
}
FUNC_RE = re.compile(r"`(?:[\w:]+::)?([A-Za-z_]\w*)\([^`]*\)`")
for doc in DOCS:
    for m in FUNC_RE.finditer(read(doc)):
        name = m.group(1)
        if name in LIBRARY or name.startswith(("lv_", "LV_", "WiFi", "esp_", "x")):
            continue
        if not re.search(r"\b%s\b" % re.escape(name), code):
            problems.append(f"{doc}: function `{name}()` not found in the code")

# ---- Faces -----------------------------------------------------------------------------------------
ui_h = read("src/ui/ui.h")
enum = re.search(r"enum Face \{(.*?)FACE_COUNT", ui_h, re.S).group(1)
enum_faces = [n[len("FACE_"):].lower() for n in re.findall(r"\b(FACE_\w+)", enum)]
table = re.search(r"FACES\[FACE_COUNT\] = \{(.*?)\};", read("src/ui/faces.cpp"), re.S).group(1)
table_faces = [n.lower() for n in re.findall(r"&faceDef(\w+)", table)]
files = sorted(os.path.basename(f)[len("face_"):-len(".cpp")] for f in glob.glob(os.path.join(ROOT, "src/ui/faces/face_*.cpp")))
if enum_faces != table_faces:
    problems.append(f"enum Face {enum_faces} and FACES[] {table_faces} differ (order matters)")
if sorted(enum_faces) != files:
    problems.append(f"enum Face {sorted(enum_faces)} and src/ui/faces/face_*.cpp {files} differ")

# ---- Web flasher palettes --------------------------------------------------------------------------
theme = read("src/ui/theme.cpp")
themes = [(n, t.lower()) for n, t in re.findall(r'\{"([^"]+)", 0x\w+, 0x\w+, 0x(\w+),', theme)]
nights = [(n, h.lower()) for n, h in re.findall(r'\{"([^"]+)", 0x(\w+)\}', theme.split("NIGHTS[]", 1)[1])]
site = read("site/index.html")


def palette(key):
    m = re.search(key + r":\s*\{[^}]*options:\s*\[(.*?)\]\s*\}", site, re.S)
    return [(n, h.lower()) for n, h in re.findall(r"\['([^']+)',\s*'#(\w+)'\]", m.group(1))] if m else None


if palette("day") != themes:
    problems.append(f"site/index.html PALETTES.day {palette('day')} != THEMES[] name + tick {themes}")
if palette("night") != nights:
    problems.append(f"site/index.html PALETTES.night {palette('night')} != NIGHTS[] {nights}")

if problems:
    print("check.py found problems:")
    for p in problems:
        print("  - " + p)
    sys.exit(1)
print(f"check.py: OK ({len(DOCS)} docs, {len(enum_faces)} faces, {len(themes)} themes, {len(nights)} night colours)")
