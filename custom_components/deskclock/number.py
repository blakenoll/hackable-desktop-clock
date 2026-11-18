"""Numeric settings without a list of choices: the fixed night-mode hours."""

from __future__ import annotations

from typing import Any

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeskClockConfigEntry, DeskClockCoordinator
from .entity import DeskClockEntity, exposed


async def async_setup_entry(
    hass: HomeAssistant, entry: DeskClockConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    async_add_entities(
        DeskClockNumber(coordinator, f)
        for f in coordinator.fields
        if exposed(f) and f["type"] == "number" and not f.get("choices")
    )


class DeskClockNumber(DeskClockEntity, NumberEntity):
    _attr_mode = NumberMode.BOX
    _attr_native_step = 1

    def __init__(self, coordinator: DeskClockCoordinator, field: dict[str, Any]) -> None:
        super().__init__(coordinator, field)
        self._attr_native_min_value = field.get("min", 0)
        self._attr_native_max_value = field.get("max", 255)

    @property
    def native_value(self) -> float | None:
        return self.value

    async def async_set_native_value(self, value: float) -> None:
        await self.coordinator.async_set(self.key, int(value))
