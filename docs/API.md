# Settings API

A small JSON API over WiFi for reading and changing the clock's settings without going through setup mode, so that apps and scripts can configure the clock. The code is in `src/api/` (`api.cpp`). It is built on `config_fields()` (`src/config_fields.h`), so every setting on the setup page is also in the API, with the same key, default and range. A setting you add there appears here automatically.

## Finding the clock

Once the clock has joined your WiFi, it serves the API on port 80 (`API_PORT` in `src/tuning.h`) and advertises itself over mDNS:

- Hostname `deskclock-xxxx.local`: `API_HOSTNAME_PREFIX` plus the same four MAC hex digits as the `clk-setup-XXXX` network, in lowercase. It is also the DHCP hostname, so it shows up under that name in your router.
- Bonjour service `_deskclock._tcp` with TXT record `api=<version>`, plus `_http._tcp`.

The setup page shows the exact address at the bottom, and serial prints it at boot (`Settings API at http://...`).

## The API key

In normal mode, every request to `/api/config` needs the header `Authorization: Bearer <key>`. The key is 16 hex characters, created on first use and kept in NVS (`apitoken`, see `config_api_token()` in `src/config.cpp`). It survives reflashing and changes only when NVS is erased.

**Where to find it:** the bottom of the setup page (hold BOOT 3 s, join `clk-setup-XXXX`). Then leave setup mode without saving by tapping **Save & restart** with nothing changed, or wait for the idle reboot. The key is never printed to serial.

The API is plain HTTP on your local network, so treat the key like a WiFi password for the clock and use it only on networks you trust.

## Endpoints

| Method and path | Key needed | What it does |
|---|---|---|
| `GET /api/info` | no | `{"api":1,"host":"deskclock-a1b2","mode":"normal","auth":true}` |
| `GET /api/schema` | no | Every setting: `key`, `type` (`text` / `secret` / `choice` / `bool` / `number`), `default`, `min` / `max`, `restart`, and `choices` (`value` + `label`) where there's a list. Faces, themes, night colours and timezones come from the firmware's own tables, so clients needn't hard-code them. |
| `GET /api/config` | yes | The current settings as one flat object keyed like the schema. The WiFi password (`pass`) is never returned. |
| `POST /api/config` | yes | Changes the keys in the JSON body (a partial object; absent keys are unchanged). Replies `{"ok":true,"restart":false,"config":{...}}`. |

Errors are `{"error":"..."}` with status 400 (bad JSON, unknown key, value out of range: nothing is saved), 401 (missing or wrong key) or 404.

**Applying changes.** Display and night settings and the timezone take effect immediately. The face rebuilds on the next main-loop pass. Settings with `"restart": true` in the schema are WiFi (`ssid`, `pass`), location (`city`, `addr`, `tidestn`) and `units`. They are only read at boot, so changing one saves it, replies with `"restart": true`, and restarts the clock about a second later. If the new WiFi details are wrong, the clock falls back to setup mode as usual.

## Examples

```sh
CLOCK=http://deskclock-a1b2.local
KEY=0123456789abcdef

curl $CLOCK/api/info
curl $CLOCK/api/schema
curl -H "Authorization: Bearer $KEY" $CLOCK/api/config
# Radar face, mono theme, night from 21:00 to 06:00
curl -H "Authorization: Bearer $KEY" -H 'Content-Type: application/json' \
     -d '{"face":9,"accent":5,"nmode":1,"nstart":21,"nend":6}' $CLOCK/api/config
```

Send `Content-Type: application/json`. A form-encoded body is not read as JSON.

## In setup mode

The same endpoints also run on the setup access point (`http://192.168.4.1/api/...`), so an app can do first-time setup without the HTML page:

- No key is needed: joining the access point already takes the password shown on the display.
- `GET /api/config` also returns `token`, the API key, so the app can keep talking to the clock after it joins your WiFi.
- `POST /api/config` must leave `ssid` set. Every save restarts the clock into normal mode.

## Implementation notes

- Requests are answered from `loop()` (`api_poll()` in `src/main.cpp`) or the setup portal's loop, never from a background task. Settings changes can therefore call LVGL directly (`settingsChanged()` in `src/main.cpp`).
- Reading, writing and describing settings are three visitors over `config_fields()` (`JsonReader`, `JsonWriter`, `SchemaWriter` in `src/api/api.cpp`). `textProblem()` holds the server-side checks for text settings (the setup page's HTML patterns only check in the browser). `RESTART_KEYS` lists the settings that need a restart.
- JSON is handled by [ArduinoJson](https://arduinojson.org) 7 (pinned in `platformio.ini`).
- Bump `API_VERSION` in `api.cpp` when a change would break existing clients.
