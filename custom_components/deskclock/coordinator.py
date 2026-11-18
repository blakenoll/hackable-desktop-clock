"""Polls the clock's settings and pushes changes back."""

from __future__ import annotations

import logging
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryAuthFailed, HomeAssistantError
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import DeskClockApi, DeskClockAuthError, DeskClockError
from .const import DOMAIN, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)

type DeskClockConfigEntry = ConfigEntry[DeskClockCoordinator]


class DeskClockCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    """Holds the clock's info, schema and current settings."""

    config_entry: DeskClockConfigEntry

    def __init__(
        self,
        hass: HomeAssistant,
        entry: DeskClockConfigEntry,
        api: DeskClockApi,
        info: dict[str, Any],
        schema: dict[str, Any],
    ) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=DOMAIN, update_interval=SCAN_INTERVAL)
        self.api = api
        self.info = info
        self.schema = schema
        self._was_offline = False

    @property
    def fields(self) -> list[dict[str, Any]]:
        return self.schema.get("fields", [])

    async def _async_update_data(self) -> dict[str, Any]:
        try:
            if self._was_offline:
                await self._check_reflashed()
            data = await self.api.config()
        except DeskClockAuthError as err:
            raise ConfigEntryAuthFailed("The clock rejected the API key") from err
        except DeskClockError as err:
            self._was_offline = True
            raise UpdateFailed(str(err)) from err
        self._was_offline = False
        return data

    async def _check_reflashed(self) -> None:
        """After the clock was unreachable (a restart or a reflash), reload if its faces or settings changed."""
        info, schema = await self.api.info(), await self.api.schema()
        if schema != self.schema or info.get("version") != self.info.get("version"):
            _LOGGER.info("Clock firmware or settings list changed; reloading")
            self.hass.config_entries.async_schedule_reload(self.config_entry.entry_id)

    async def async_set(self, key: str, value: Any) -> None:
        """Change one setting on the clock."""
        try:
            resp = await self.api.set({key: value})
        except DeskClockAuthError as err:
            self.config_entry.async_start_reauth(self.hass)
            raise HomeAssistantError("The clock rejected the API key") from err
        except DeskClockError as err:
            raise HomeAssistantError(f"Couldn't change {key}: {err}") from err
        if resp.get("restart"):
            _LOGGER.info("The clock restarts to apply %s", key)
        self.async_set_updated_data(resp.get("config", self.data))
