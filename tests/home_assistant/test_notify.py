"""Notifications: the notify entity and the show / dismiss services."""

import pytest
from homeassistant.const import CONF_API_KEY, CONF_HOST
from homeassistant.exceptions import HomeAssistantError
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.deskclock.const import DOMAIN

from .conftest import DEVICE_ID, HOST, KEY

ENTITY = "notify.desk_clock_a1b2_notification"


async def _setup(hass):
    e = MockConfigEntry(domain=DOMAIN, unique_id=DEVICE_ID, title="Desk Clock A1B2",
                        data={CONF_HOST: HOST, CONF_API_KEY: KEY})
    e.add_to_hass(hass)
    assert await hass.config_entries.async_setup(e.entry_id)
    await hass.async_block_till_done()
    return e


async def test_send_message(hass, clock):
    await _setup(hass)
    await hass.services.async_call("notify", "send_message",
                                   {"entity_id": ENTITY, "message": "Washer done", "title": "Laundry"}, blocking=True)
    assert clock.card == {"title": "Laundry", "text": "Washer done", "level": "info"}


async def test_show_with_level_duration_and_id(hass, clock):
    await _setup(hass)
    await hass.services.async_call(DOMAIN, "show_notification",
                                   {"entity_id": ENTITY, "message": "Door open", "level": "alarm", "duration": 0, "id": "door"},
                                   blocking=True)
    assert clock.card == {"text": "Door open", "level": "alarm", "duration": 0, "id": "door"}


async def test_dismiss(hass, clock):
    await _setup(hass)
    await hass.services.async_call(DOMAIN, "show_notification", {"entity_id": ENTITY, "message": "Hi", "id": "a"}, blocking=True)
    await hass.services.async_call(DOMAIN, "dismiss_notification", {"entity_id": ENTITY, "id": "b"}, blocking=True)
    assert clock.card is not None  # another card's id: left alone
    await hass.services.async_call(DOMAIN, "dismiss_notification", {"entity_id": ENTITY}, blocking=True)
    assert clock.card is None


async def test_error_from_the_clock(hass, clock):
    await _setup(hass)
    with pytest.raises(HomeAssistantError, match="at most 80"):
        await hass.services.async_call("notify", "send_message", {"entity_id": ENTITY, "message": "x" * 81}, blocking=True)


async def test_old_firmware_has_no_notify_entity(hass, clock):
    clock.notify_support = False
    await _setup(hass)
    assert hass.states.get(ENTITY) is None
