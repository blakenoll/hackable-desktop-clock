"""Set up a clock: found automatically over mDNS (_deskclock._tcp), or by host name / IP."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

import voluptuous as vol

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_API_KEY, CONF_HOST
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from .api import DeskClockApi, DeskClockAuthError, DeskClockError
from .const import DOMAIN

KEY_SCHEMA = vol.Schema({vol.Required(CONF_API_KEY): str})


def _title(info: dict[str, Any]) -> str:
    return f"Desk Clock {str(info.get('host', info['id'])).rsplit('-', 1)[-1].upper()}"


class DeskClockConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._host: str | None = None
        self._name: str | None = None

    async def _validate(self, host: str, api_key: str) -> tuple[dict[str, Any] | None, str | None]:
        """Returns (info, None) when the key works, else (None, error key)."""
        api = DeskClockApi(async_get_clientsession(self.hass), host, api_key)
        try:
            info = await api.info()
            if info.get("mode") != "normal":
                return None, "setup_mode"
            await api.config()
        except DeskClockAuthError:
            return None, "invalid_auth"
        except DeskClockError:
            return None, "cannot_connect"
        return info, None

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            key = user_input[CONF_API_KEY].strip()
            info, error = await self._validate(host, key)
            if info:
                await self.async_set_unique_id(info["id"])
                self._abort_if_unique_id_configured(updates={CONF_HOST: host, CONF_API_KEY: key})
                return self.async_create_entry(title=_title(info), data={CONF_HOST: host, CONF_API_KEY: key})
            errors["base"] = error
        schema = vol.Schema({vol.Required(CONF_HOST): str, vol.Required(CONF_API_KEY): str})
        return self.async_show_form(
            step_id="user", data_schema=self.add_suggested_values_to_schema(schema, user_input), errors=errors
        )

    async def async_step_zeroconf(self, discovery_info: ZeroconfServiceInfo) -> ConfigFlowResult:
        if discovery_info.ip_address.version != 4:
            return self.async_abort(reason="not_ipv4")
        host = discovery_info.host if discovery_info.port in (None, 80) else f"{discovery_info.host}:{discovery_info.port}"
        device_id = discovery_info.properties.get("id")
        if not device_id:  # firmware from before the TXT record: ask the clock
            try:
                device_id = (await DeskClockApi(async_get_clientsession(self.hass), host).info())["id"]
            except (DeskClockError, KeyError):
                return self.async_abort(reason="cannot_connect")
        await self.async_set_unique_id(device_id)
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})  # follows a new DHCP address
        self._host = host
        self._name = f"Desk Clock {discovery_info.hostname.split('.')[0].rsplit('-', 1)[-1].upper()}"
        self.context["title_placeholders"] = {"name": self._name}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        assert self._host
        errors: dict[str, str] = {}
        if user_input is not None:
            key = user_input[CONF_API_KEY].strip()
            info, error = await self._validate(self._host, key)
            if info:
                return self.async_create_entry(title=_title(info), data={CONF_HOST: self._host, CONF_API_KEY: key})
            errors["base"] = error
        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=KEY_SCHEMA,
            errors=errors,
            description_placeholders={"name": self._name or self._host, "host": self._host},
        )

    async def async_step_reauth(self, entry_data: Mapping[str, Any]) -> ConfigFlowResult:
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        entry = self._get_reauth_entry()
        errors: dict[str, str] = {}
        if user_input is not None:
            key = user_input[CONF_API_KEY].strip()
            info, error = await self._validate(entry.data[CONF_HOST], key)
            if info:
                await self.async_set_unique_id(info["id"])
                self._abort_if_unique_id_mismatch()
                return self.async_update_reload_and_abort(entry, data_updates={CONF_API_KEY: key})
            errors["base"] = error
        return self.async_show_form(
            step_id="reauth_confirm",
            data_schema=KEY_SCHEMA,
            errors=errors,
            description_placeholders={"name": entry.title},
        )
