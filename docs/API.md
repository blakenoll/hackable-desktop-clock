# Settings API

A small JSON API over WiFi for reading and changing the clock's settings without going through setup mode, so that apps and scripts can configure the clock. The code is in `src/api/` (`api.cpp`). It is built on `config_fields()` (`src/config_fields.h`), so every setting on the setup page is also in the API, with the same key, default and range. A setting you add there appears here automatically.

## Finding the clock

Once the clock has joined your WiFi, it serves the API on port 80 (`API_PORT` in `src/tuning.h`) and advertises itself over mDNS:

- Hostname `deskclock-xxxx.local`: `API_HOSTNAME_PREFIX` plus the same four MAC hex digits as the `clk-setup-XXXX` network, in lowercase. It is also the DHCP hostname, so it shows up under that name in your router.
- Bonjour service `_deskclock._tcp` with TXT records `api=<version>` and `id=<MAC in hex>`, plus `_http._tcp`. Home Assistant uses this to discover clocks ([HOME_ASSISTANT.md](HOME_ASSISTANT.md)).

The setup page shows the exact address at the bottom, and serial prints it at boot (`Settings API at http://...`).

## The API key

In normal mode, every request to `/api/config` needs the header `Authorization: Bearer <key>`. The key is 16 hex characters, created on first use and kept in NVS (`apitoken`, see `config_api_token()` in `src/config.cpp`). It survives reflashing and changes only when NVS is erased.

**Where to find it:** a client can ask for it by [pairing](#pairing), with a press of BOOT. Or read it at the bottom of the setup page (hold BOOT 3 s, join `clk-setup-XXXX`). Then leave setup mode without saving by tapping **Save & restart** with nothing changed, or wait for the idle reboot. The key is never printed to serial.

The API is plain HTTP on your local network, so treat the key like a WiFi password for the clock and use it only on networks you trust.

## Pairing

Instead of copying the key, a client can ask the clock for it and have someone confirm on the clock. Home Assistant does this ([HOME_ASSISTANT.md](HOME_ASSISTANT.md)).

1. The client sends `POST /api/pair` (no key, no body). The clock shows a card with a random 4-digit code and replies `202 {"session":"<16 hex>","code":"4821","expires":60}`. The client shows the same code to the user. The window is `PAIR_WINDOW_SECONDS` (`src/tuning.h`).
2. The person at the clock checks that the codes match and **presses BOOT** (a double press cancels). Both presses are handled in `handleButton()` and don't change the face.
3. The client polls `GET /api/pair?session=<session>` and gets `{"state":"pending","expires":41}`, then one of:
   - `{"state":"approved","token":"<the API key>"}`: handed over once (asking again is a 404), and forgotten after 30 s if nobody collects it.
   - `{"state":"denied"}` (double press) or `{"state":"expired"}` (no press in time).

Only one request can be open: a second `POST /api/pair` while one is pending gets 409, so nobody can swap theirs in. The key is returned only to whoever holds the `session`, and only after a physical press. The code guards against a request you didn't start: if the card shows a code your client isn't displaying, don't press. Like the rest of the API this is plain HTTP, so the key crosses your network in the clear. Pairing is not available in setup mode (`"pair": false` in `/api/info`), where `GET /api/config` already returns the key.

## Endpoints

| Method and path | Key needed | What it does |
|---|---|---|
| `GET /api/info` | no | `{"api":1,"id":"a0b1c2d3a1b2","version":"9685f64","host":"deskclock-a1b2","mode":"normal","auth":true,"notify":true}`. `id` is the MAC (a stable unique ID); `version` is `git describe` of the build (`git_version.py`, "dev" without git); `notify` says `POST /api/notify` exists (firmware from before notifications has no such key), and `pair` says the same for `POST /api/pair`. |
| `GET /api/schema` | no | Every setting: `key`, `type` (`text` / `secret` / `choice` / `bool` / `number`), `default`, `min` / `max`, `restart`, and `choices` (`value` + `label`) where there's a list. Faces, themes, night colours and timezones come from the firmware's own tables, so clients needn't hard-code them. |
| `GET /api/config` | yes | The current settings as one flat object keyed like the schema. The WiFi password (`pass`) is never returned. |
| `POST /api/config` | yes | Changes the keys in the JSON body (a partial object; absent keys are unchanged). Replies `{"ok":true,"restart":false,"config":{...}}`. |

| `POST /api/pair` | no | Starts pairing ([above](#pairing)). Replies `202` with the `session` and `code`; 409 if one is already open. |
| `GET /api/pair?session=` | the session | Reports the pairing request, and hands over the key once it is approved. |
| `POST /api/notify` | yes | Shows a notification card over the face ([below](#notifications)). Replies `{"ok":true}`. |
| `DELETE /api/notify` | yes | Removes the card, optionally only if it has the id in `?id=`. Replies `{"ok":true,"cleared":true}`. |

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

## Notifications

`POST /api/notify` puts a card over whatever face is showing: an icon, a title and a few lines of text, inside a ring (previews in [faces/notify.png](faces/notify.png)). One card shows at a time, and a new one replaces it.

| Field | Meaning |
|---|---|
| `title` | Up to 30 characters, one line. |
| `text` | Up to 80 characters, wraps over about four lines. Send a `title`, a `text` or both. |
| `level` | `"info"` (default): green, whatever the theme. `"warn"`: amber. `"alarm"`: bright red, pulses, and ignores night mode. Info and warn follow the night colour at night. |
| `duration` | Seconds on screen, 0 to 3600 (default 30, `NOTIFY_DEFAULT_SECONDS` in `src/tuning.h`). **0 keeps the card until it is dismissed.** The ring unwinds as the time runs out. |
| `id` | Up to 24 characters. A name for the card, so `DELETE /api/notify?id=<id>` removes just that one. |

A short or double press of BOOT dismisses the card (it doesn't change the face meanwhile); holding BOOT still opens setup. The fonts only have printable ASCII and the degree sign, so other characters show as `?`. Notifications aren't available in setup mode (409).

```sh
curl -H "Authorization: Bearer $KEY" -H 'Content-Type: application/json' \
     -d '{"title":"Front door","text":"Someone is at the door.","level":"warn","duration":20,"id":"door"}' $CLOCK/api/notify
curl -X DELETE -H "Authorization: Bearer $KEY" "$CLOCK/api/notify?id=door"
```

The code is `src/ui/notify.cpp` (`ui_notify()` in `ui.h`). Cards are not saved: a restart clears them.

## In setup mode

The same endpoints also run on the setup access point (`http://192.168.4.1/api/...`), so an app can do first-time setup without the HTML page:

- No key is needed: joining the access point already takes the password shown on the display.
- `GET /api/config` also returns `token`, the API key, so the app can keep talking to the clock after it joins your WiFi.
- `POST /api/config` must leave `ssid` set. Every save restarts the clock into normal mode.

## Implementation notes

- Requests are answered from `loop()` (`api_poll()` in `src/main.cpp`) or the setup portal's loop, never from a background task. Settings changes and notifications can therefore call LVGL directly (`settingsChanged()` in `src/main.cpp`).
- Reading, writing and describing settings are three visitors over `config_fields()` (`JsonReader`, `JsonWriter`, `SchemaWriter` in `src/api/api.cpp`). `textProblem()` holds the server-side checks for text settings (the setup page's HTML patterns only check in the browser). `RESTART_KEYS` lists the settings that need a restart.
- JSON is handled by [ArduinoJson](https://arduinojson.org) 7 (pinned in `platformio.ini`).
- Bump `API_VERSION` in `api.cpp` when a change would break existing clients.
