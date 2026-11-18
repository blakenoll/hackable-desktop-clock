"""Base entity: one setting of one clock, described by its /api/schema field."""

from __future__ import annotations

from typing import Any

from homeassistant.const import EntityCategory
from homeassistant.helpers.device_registry import CONNECTION_NETWORK_MAC, DeviceInfo, format_mac
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN, MANUFACTURER, MODEL, PRIMARY_KEYS
from .coordinator import DeskClockCoordinator

# Settings with a name and icon in translations/en.json and icons.json. A setting added to the firmware later
# still gets an entity (named after its key) without changing this integration.
KNOWN_KEYS = {
    "tz", "units", "h24", "datefmt", "accent", "face", "nmode", "nstart", "nend", "ncolor", "chime", "detail", "secs",
}


def exposed(field: dict[str, Any]) -> bool:
    """Every setting except the WiFi password and free text (WiFi name, ZIP, address, tide station).

    Those stay on the clock's setup page: a typo there takes the clock offline or loses the weather.
    """
    kind = field.get("type")
    return kind != "secret" and not (kind == "text" and not field.get("choices"))


class DeskClockEntity(CoordinatorEntity[DeskClockCoordinator]):
    """A setting of the clock."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: DeskClockCoordinator, field: dict[str, Any]) -> None:
        super().__init__(coordinator)
        self.field = field
        self.key: str = field["key"]
        info = coordinator.info
        device_id = info["id"]
        self._attr_unique_id = f"{device_id}_{self.key}"
        if self.key in KNOWN_KEYS:
            self._attr_translation_key = self.key
        else:
            self._attr_name = self.key
        if self.key not in PRIMARY_KEYS:
            self._attr_entity_category = EntityCategory.CONFIG
        suffix = str(info.get("host", device_id)).rsplit("-", 1)[-1].upper()
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, device_id)},
            connections={(CONNECTION_NETWORK_MAC, format_mac(device_id))},
            name=f"Desk Clock {suffix}",
            manufacturer=MANUFACTURER,
            model=MODEL,
            sw_version=info.get("version"),
        )

    @property
    def available(self) -> bool:
        return super().available and self.key in (self.coordinator.data or {})

    @property
    def value(self) -> Any:
        return self.coordinator.data.get(self.key)
