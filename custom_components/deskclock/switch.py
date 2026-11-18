"""On/off settings: 24-hour time, hourly chime, weather detail line, seconds as digits."""

from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeskClockConfigEntry
from .entity import DeskClockEntity, exposed


async def async_setup_entry(
    hass: HomeAssistant, entry: DeskClockConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    async_add_entities(DeskClockSwitch(coordinator, f) for f in coordinator.fields if exposed(f) and f["type"] == "bool")


class DeskClockSwitch(DeskClockEntity, SwitchEntity):
    @property
    def is_on(self) -> bool | None:
        return self.value

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self.coordinator.async_set(self.key, True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.coordinator.async_set(self.key, False)
