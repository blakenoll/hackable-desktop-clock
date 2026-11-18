"""Entities built from the schema, reading and changing settings."""

from datetime import timedelta

import pytest
from homeassistant.config_entries import ConfigEntryState
from homeassistant.const import CONF_API_KEY, CONF_HOST
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr, entity_registry as er
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import MockConfigEntry, async_fire_time_changed

from custom_components.deskclock.const import DOMAIN

from .conftest import DEVICE_ID, HOST, KEY


@pytest.fixture
async def entry(hass, clock):
    e = MockConfigEntry(domain=DOMAIN, unique_id=DEVICE_ID, title="Desk Clock A1B2",
                        data={CONF_HOST: HOST, CONF_API_KEY: KEY})
    e.add_to_hass(hass)
    assert await hass.config_entries.async_setup(e.entry_id)
    await hass.async_block_till_done()
    return e


async def test_entities_from_schema(hass, entry):
    ents = er.async_get(hass)
    ids = {e.unique_id.removeprefix(f"{DEVICE_ID}_"): e.entity_id
           for e in er.async_entries_for_config_entry(ents, entry.entry_id)}
    # Every setting except the password and free text (ssid, city).
    assert set(ids) == {"tz", "units", "h24", "face", "accent", "nstart", "chime"}
    assert hass.states.get(ids["face"]).state == "Polished"
    assert hass.states.get(ids["face"]).attributes["options"] == ["Polished", "Arc", "Radar"]
    assert hass.states.get(ids["tz"]).state == "US Pacific"
    assert hass.states.get(ids["chime"]).state == "on"
    assert hass.states.get(ids["nstart"]).state == "22"
    assert ids["face"] == "select.desk_clock_a1b2_clock_face"
    assert ents.async_get(ids["face"]).entity_category is None
    assert ents.async_get(ids["tz"]).entity_category == "config"

    [device] = dr.async_entries_for_config_entry(dr.async_get(hass), entry.entry_id)
    assert device.sw_version == "abc1234"
    assert device.name == "Desk Clock A1B2"


async def test_change_settings(hass, entry, clock):
    await hass.services.async_call("select", "select_option",
                                   {"entity_id": "select.desk_clock_a1b2_clock_face", "option": "Radar"}, blocking=True)
    assert clock.config["face"] == 2
    assert hass.states.get("select.desk_clock_a1b2_clock_face").state == "Radar"

    await hass.services.async_call("switch", "turn_off", {"entity_id": "switch.desk_clock_a1b2_hourly_chime"},
                                   blocking=True)
    assert clock.config["chime"] is False

    await hass.services.async_call("number", "set_value",
                                   {"entity_id": "number.desk_clock_a1b2_night_starts_hour", "value": 21}, blocking=True)
    assert clock.config["nstart"] == 21

    await hass.services.async_call("select", "select_option",
                                   {"entity_id": "select.desk_clock_a1b2_timezone", "option": "UTC"}, blocking=True)
    assert clock.config["tz"] == "UTC0"


async def test_change_rejected(hass, entry, clock):
    clock.schema["fields"][6]["max"] = 0  # the clock now rejects face 2
    with pytest.raises(HomeAssistantError):
        await hass.services.async_call("select", "select_option",
                                       {"entity_id": "select.desk_clock_a1b2_clock_face", "option": "Radar"},
                                       blocking=True)


async def test_button_press_on_clock_shows_up(hass, entry, clock):
    clock.config["face"] = 1
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=31))
    await hass.async_block_till_done()
    assert hass.states.get("select.desk_clock_a1b2_clock_face").state == "Arc"


async def test_offline_then_reflashed_reloads(hass, entry, clock):
    clock.online = False
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=31))
    await hass.async_block_till_done()
    assert hass.states.get("select.desk_clock_a1b2_clock_face").state == "unavailable"

    clock.online = True
    clock.version = "def5678"
    clock.schema["fields"][6]["choices"].append({"value": 3, "label": "Lunar"})
    clock.schema["fields"][6]["max"] = 3
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=62))
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED
    assert hass.states.get("select.desk_clock_a1b2_clock_face").attributes["options"][-1] == "Lunar"
    [device] = dr.async_entries_for_config_entry(dr.async_get(hass), entry.entry_id)
    assert device.sw_version == "def5678"


async def test_wrong_key_starts_reauth(hass, entry, clock):
    clock.key = "fedcba9876543210"
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=31))
    await hass.async_block_till_done()
    flows = hass.config_entries.flow.async_progress()
    assert [f["context"]["source"] for f in flows] == ["reauth"]


async def test_setup_mode_not_ready(hass, clock):
    clock.mode = "setup"
    e = MockConfigEntry(domain=DOMAIN, unique_id=DEVICE_ID, data={CONF_HOST: HOST, CONF_API_KEY: KEY})
    e.add_to_hass(hass)
    await hass.config_entries.async_setup(e.entry_id)
    assert e.state is ConfigEntryState.SETUP_RETRY


async def test_unload(hass, entry):
    assert await hass.config_entries.async_unload(entry.entry_id)
    assert entry.state is ConfigEntryState.NOT_LOADED
