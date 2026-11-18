"""Constants for the Hackable Desk Clock integration."""

from datetime import timedelta

DOMAIN = "deskclock"
MANUFACTURER = "Hackable Desk Clock"
MODEL = "ESP32-C3 round desk clock"

SCAN_INTERVAL = timedelta(seconds=30)
REQUEST_TIMEOUT = 10  # seconds

# Settings shown on the device card's Controls; every other one is under Configuration.
PRIMARY_KEYS = {"face", "accent"}
