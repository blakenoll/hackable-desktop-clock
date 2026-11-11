#pragma once

// Starts a WPA2 access point with a random password, shows the network name and password on the
// display, and serves a captive-portal page for WiFi + timezone + display options. Saves to NVS (see
// config.h) and reboots on submit; never returns.
// hasSaved: if true, reboots after a few idle minutes to retry saved settings.
[[noreturn]] void prov_run(bool hasSaved);
