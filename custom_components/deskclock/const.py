"""Constants for the Hackable Desk Clock integration."""

from datetime import timedelta

DOMAIN = "deskclock"
MANUFACTURER = "Hackable Desk Clock"
MODEL = "ESP32-C3 round desk clock"

SCAN_INTERVAL = timedelta(seconds=30)
REQUEST_TIMEOUT = 10  # seconds

# Settings shown on the device card's Controls; every other one is under Configuration.
PRIMARY_KEYS = {"face", "accent"}

# Notification cards (notify.py). Levels and the default duration are the firmware's (POST /api/notify, docs/API.md).
NOTIFY_LEVELS = ["info", "warn", "alarm"]

# Pairing by BOOT press (config_flow.py, POST /api/pair in docs/API.md).
PAIR_POLL_INTERVAL = 2  # seconds between checks while the clock waits for the button
PAIR_GIVE_UP = 90  # seconds; the clock's own window is PAIR_WINDOW_SECONDS (60) in src/tuning.h
