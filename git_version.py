# Pre-build: FIRMWARE_VERSION = `git describe` of this checkout (e.g. "9685f64" or "9685f64-dirty"), shown by
# GET /api/info (docs/API.md) and as the firmware version on the Home Assistant device page. "dev" without git.
Import("env")
import subprocess

try:
    ver = subprocess.check_output(["git", "describe", "--always", "--dirty"], cwd=env.subst("$PROJECT_DIR"),
                                  stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    ver = ""
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(ver or "dev"))])
