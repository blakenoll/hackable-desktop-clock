# Home Assistant

The `deskclock` integration (`custom_components/deskclock/` in this repo) lets Home Assistant find clocks on your network automatically and shows their settings as entities, so you can change them from dashboards and automations. It talks to the clock's settings API ([API.md](API.md)) and needs no extra firmware or broker.

## Install

**With HACS:**
1. HACS → ⋮ → **Custom repositories**.
2. Add `https://github.com/blakenoll/hackable-desktop-clock`, type **Integration**.
3. Install **Hackable Desk Clock**, then restart Home Assistant.

**By hand:** copy `custom_components/deskclock/` into your Home Assistant config folder, so that `<config>/custom_components/deskclock/manifest.json` exists. Then restart Home Assistant. For the Docker image, `<config>` is the folder you mount at `/config`.

## Add a clock

The clock needs firmware with the settings API (anything from PR #29 on).

- **Found automatically.** A clock on your WiFi appears under Settings → Devices & services as **Discovered: Desk Clock A1B2**. Click **Add**, then **Submit**.
- **By hand.** Add integration → **Hackable Desk Clock**, then enter `deskclock-xxxx.local` (or the clock's IP).
- **Pair.** The clock shows a card "Pair with app?" with a 4-digit code, and Home Assistant shows the same code. If they match, press BOOT on the clock (double-press to cancel). You have 60 seconds. Home Assistant receives the API key from the clock and stores it, so there is nothing to copy ([API.md](API.md#pairing)). Nothing appears on the clock until you click **Submit**.
- **Typing the key instead.** If pairing fails you can choose **Enter the API key instead**. Firmware from before pairing goes straight to that form. The key is at the bottom of the clock's setup page: hold BOOT for 3 seconds, join `clk-setup-XXXX`, and read it off the page ([API.md](API.md#the-api-key)).

**Docker:** auto-discovery needs the container on the host network (`network_mode: host`, which Home Assistant recommends anyway). On a bridged network, mDNS doesn't reach the container, so add the clock by IP instead.

## What you get

One device per clock, with the firmware version (`git describe` of the build) on its device page, and one entity per setting:

| Entity | Settings |
|---|---|
| Select | Clock face and colour theme (on the device's Controls); timezone, date format, night mode, night colour, temperature units (under Configuration) |
| Switch | 24-hour time, hourly chime, weather detail line, seconds as digits |
| Number | Night starts / night ends (hours, for fixed-hours night mode) |
| Notify | **Notification**: messages shown on the clock's screen ([below](#notifications)) |

- **Option names come from the clock.** The faces, themes, night colours and timezones are the firmware's own lists from `/api/schema`. A face or setting you add to the firmware shows up after the next reflash: the integration reloads by itself when it sees a new firmware version after the clock was offline.
- **Not exposed:** the WiFi name and password, ZIP code, street address and tide station. A typo there would take the clock offline or lose the weather, so those stay on the setup page.
- **Units** is exposed, but changing it restarts the clock (about 10 s unavailable). The schema's `restart` flag says which settings do this.

The integration polls every 30 seconds (`SCAN_INTERVAL` in `custom_components/deskclock/const.py`), so a face changed with the BOOT button shows up in Home Assistant within that time.

**Example automation:** the Radar face while the dog is out, otherwise Polished.

```yaml
automation:
  - alias: Desk clock follows the house
    triggers:
      - trigger: state
        entity_id: input_boolean.dog_out
    actions:
      - action: select.select_option
        target:
          entity_id: select.desk_clock_a1b2_clock_face
        data:
          option: "{{ 'Radar' if trigger.to_state.state == 'on' else 'Polished' }}"
```

## Notifications

The **Notification** entity shows messages as a card over the clock face ([API.md](API.md#notifications) has the details and limits: 30 characters of title, 80 of text, plain ASCII). It appears only for firmware that has them; reflash the clock if it's missing.

The standard action works for a plain message (an info card for 30 seconds):

```yaml
action: notify.send_message
target:
  entity_id: notify.desk_clock_a1b2_notification
data:
  title: Laundry done
  message: The washing machine has finished.
```

`deskclock.show_notification` adds a `level` (`info`, `warn`, `alarm`), a `duration` in seconds (0 = until dismissed) and an `id`. `deskclock.dismiss_notification` removes the card, or only the one with a given `id`. Pressing BOOT on the clock dismisses it too.

```yaml
automation:
  - alias: Garage left open
    triggers:
      - trigger: state
        entity_id: cover.garage_door
        to: "open"
        for: "00:10:00"
    actions:
      - action: deskclock.show_notification
        target:
          entity_id: notify.desk_clock_a1b2_notification
        data:
          title: Garage open
          message: Open for 10 minutes.
          level: warn
          duration: 0
          id: garage
  - alias: Garage closed
    triggers:
      - trigger: state
        entity_id: cover.garage_door
        to: "closed"
    actions:
      - action: deskclock.dismiss_notification
        target:
          entity_id: notify.desk_clock_a1b2_notification
        data:
          id: garage
```

## Without the integration

Home Assistant's built-in `rest_command` can call the API directly. There's no discovery or device page, but it needs nothing installed:

```yaml
# configuration.yaml; put the key in secrets.yaml as deskclock_auth: "Bearer 0123456789abcdef"
rest_command:
  deskclock_set:
    url: http://deskclock-a1b2.local/api/config
    method: POST
    headers:
      Authorization: !secret deskclock_auth
    content_type: application/json
    payload: "{{ settings | tojson }}"
```

Call it with `action: rest_command.deskclock_set` and `data: {settings: {face: 9, accent: 5}}`. The numbers are indexes into the lists from `GET /api/schema`.

## Troubleshooting

- **Not discovered.** First restart Home Assistant if you haven't since installing the integration: it only starts watching for `_deskclock._tcp` at startup. Then check the log for warnings from `custom_components.deskclock` (an unreachable clock, or firmware too old to be added), that Home Assistant's container uses host networking (see above), and that `http://deskclock-xxxx.local/api/info` opens from the Home Assistant machine. If it doesn't, add the clock by IP. Home Assistant updates the address by itself when discovery later sees the clock at a new IP.
- **"The clock rejected the API key"** after erasing the clock's flash: the key changed. Home Assistant shows a **Reconfigure** prompt: click **Submit** and press BOOT on the clock again.
- **Entities unavailable.** The clock is offline, restarting or in setup mode. They come back on their own.

## Changing the integration

- **Files** (in `custom_components/deskclock/`):
  - `custom_components/deskclock/config_flow.py`: discovery, pairing by BOOT press (with the typed key as a fallback) and reauth.
  - `custom_components/deskclock/coordinator.py`: polling and changing a setting.
  - `custom_components/deskclock/entity.py`: which settings are exposed (`exposed()`), and the device info.
  - `custom_components/deskclock/select.py`, `custom_components/deskclock/switch.py` and `custom_components/deskclock/number.py`: build entities from the schema.
  - `custom_components/deskclock/notify.py`: the notification entity and the `show_notification` / `dismiss_notification` services (`services.yaml` and the translations describe them).
  - `custom_components/deskclock/translations/en.json` and `icons.json`: names and icons for the known settings (`KNOWN_KEYS` in `custom_components/deskclock/entity.py`). Any other setting is named after its key.
- **Icon.** `custom_components/deskclock/brand/icon.png` and `icon@2x.png` are the Polished face in its bezel, written by `sh docs/tools/sim/render.sh` (`docs/tools/sim/brand_icon.py`). Home Assistant serves them from the integration folder after a restart; versions too old to read a `brand/` folder show "icon not available".
- **Tests.** They run against a fake clock (`tests/home_assistant/conftest.py`): `pip install -r tests/home_assistant/requirements.txt && pytest tests/home_assistant`. Home Assistant needs Python 3.14. CI runs them.
- **Versions.** When you change the integration, bump `version` in `custom_components/deskclock/manifest.json`. `homeassistant` in `hacs.json` is the oldest Home Assistant release it supports.
