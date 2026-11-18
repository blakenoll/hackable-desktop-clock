"""Settings with a list of choices: face, theme, night mode, night colour, timezone, date format, units."""

from __future__ import annotations

from typing import Any

from homeassistant.components.select import SelectEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeskClockConfigEntry, DeskClockCoordinator
from .entity import DeskClockEntity, exposed


async def async_setup_entry(
    hass: HomeAssistant, entry: DeskClockConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    async_add_entities(
        DeskClockSelect(coordinator, f) for f in coordinator.fields if exposed(f) and f.get("choices")
    )


class DeskClockSelect(DeskClockEntity, SelectEntity):
    """Options are the firmware's own labels ("Radar", "Mono", "US Pacific")."""

    def __init__(self, coordinator: DeskClockCoordinator, field: dict[str, Any]) -> None:
        super().__init__(coordinator, field)
        choices = field["choices"]
        self._attr_options = [c["label"] for c in choices]
        self._value_of = {c["label"]: c["value"] for c in choices}
        self._label_of = {c["value"]: c["label"] for c in choices}

    @property
    def current_option(self) -> str | None:
        return self._label_of.get(self.value)  # None for a saved timezone that isn't a preset

    async def async_select_option(self, option: str) -> None:
        await self.coordinator.async_set(self.key, self._value_of[option])
