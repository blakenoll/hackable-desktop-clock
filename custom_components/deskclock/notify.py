"""A notify entity that shows messages on the clock's screen, plus services for level, duration and dismissing."""

from __future__ import annotations

from typing import Any

import voluptuous as vol
from homeassistant.components.notify import NotifyEntity
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import config_validation as cv, entity_platform
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .api import DeskClockAuthError, DeskClockError
from .const import NOTIFY_LEVELS
from .coordinator import DeskClockConfigEntry, DeskClockCoordinator
from .entity import device_info

SERVICE_SHOW = "show_notification"
SERVICE_DISMISS = "dismiss_notification"


async def async_setup_entry(
    hass: HomeAssistant, entry: DeskClockConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    if not coordinator.info.get("notify"):  # firmware from before notifications
        return
    async_add_entities([DeskClockNotify(coordinator)])

    platform = entity_platform.async_get_current_platform()
    platform.async_register_entity_service(
        SERVICE_SHOW,
        {
            vol.Required("message"): cv.string,
            vol.Optional("title"): cv.string,
            vol.Optional("level", default="info"): vol.In(NOTIFY_LEVELS),
            vol.Optional("duration"): vol.All(vol.Coerce(int), vol.Range(min=0, max=3600)),
            vol.Optional("id"): cv.string,
        },
        "async_show",
    )
    platform.async_register_entity_service(SERVICE_DISMISS, {vol.Optional("id"): cv.string}, "async_dismiss")


class DeskClockNotify(CoordinatorEntity[DeskClockCoordinator], NotifyEntity):
    """Messages shown as a card over the clock face until they time out or BOOT is pressed."""

    _attr_has_entity_name = True
    _attr_translation_key = "notification"

    def __init__(self, coordinator: DeskClockCoordinator) -> None:
        super().__init__(coordinator)
        self._attr_unique_id = f"{coordinator.info['id']}_notification"
        self._attr_device_info = device_info(coordinator.info)

    async def async_send_message(self, message: str, title: str | None = None) -> None:
        """notify.send_message: an info card with the default duration."""
        await self.async_show(message, title)

    async def async_show(
        self,
        message: str,
        title: str | None = None,
        level: str = "info",
        duration: int | None = None,
        id: str | None = None,  # noqa: A002 (the service field is called id)
    ) -> None:
        card: dict[str, Any] = {"text": message, "level": level}
        for key, value in (("title", title), ("duration", duration), ("id", id)):
            if value is not None:
                card[key] = value
        await self._call(self.coordinator.api.notify(card), "show the notification")

    async def async_dismiss(self, id: str | None = None) -> None:  # noqa: A002
        await self._call(self.coordinator.api.clear_notification(id), "dismiss the notification")

    async def _call(self, request, what: str) -> None:
        try:
            await request
        except DeskClockAuthError as err:
            self.coordinator.config_entry.async_start_reauth(self.hass)
            raise HomeAssistantError("The clock rejected the API key") from err
        except DeskClockError as err:
            raise HomeAssistantError(f"Couldn't {what}: {err}") from err
