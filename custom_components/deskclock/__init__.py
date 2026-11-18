"""The Hackable Desk Clock integration: the clock's settings as Home Assistant entities."""

from __future__ import annotations

from homeassistant.const import CONF_API_KEY, CONF_HOST, Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryNotReady
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import DeskClockApi, DeskClockError
from .coordinator import DeskClockConfigEntry, DeskClockCoordinator

PLATFORMS = [Platform.NUMBER, Platform.SELECT, Platform.SWITCH]


async def async_setup_entry(hass: HomeAssistant, entry: DeskClockConfigEntry) -> bool:
    api = DeskClockApi(async_get_clientsession(hass), entry.data[CONF_HOST], entry.data[CONF_API_KEY])
    try:
        info = await api.info()
        schema = await api.schema()
    except DeskClockError as err:
        raise ConfigEntryNotReady(str(err)) from err
    if info.get("mode") != "normal":
        raise ConfigEntryNotReady("The clock is in setup mode")

    coordinator = DeskClockCoordinator(hass, entry, api, info, schema)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: DeskClockConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
