"""Set up a clock: found automatically over mDNS (_deskclock._tcp), or by host name / IP. The clock hands over its
API key when someone presses BOOT on it (pairing); typing the key is the fallback for older firmware."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Mapping
from typing import Any

import voluptuous as vol

from homeassistant.config_entries import SOURCE_REAUTH, ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_API_KEY, CONF_HOST
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from .api import DeskClockApi, DeskClockAuthError, DeskClockConnectionError, DeskClockError
from .const import DOMAIN, PAIR_GIVE_UP, PAIR_POLL_INTERVAL

_LOGGER = logging.getLogger(__name__)

KEY_SCHEMA = vol.Schema({vol.Required(CONF_API_KEY): str})
REFLASH_HINT = "reflash it with current firmware (see docs/HOME_ASSISTANT.md)"


def _title(info: dict[str, Any]) -> str:
    return f"Desk Clock {str(info.get('host', info['id'])).rsplit('-', 1)[-1].upper()}"


class PairingFailed(Exception):
    """The clock didn't hand over its key; the message is shown to the user."""


class DeskClockConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._host: str | None = None
        self._name: str | None = None
        self._title: str | None = None
        self._code: str | None = None
        self._key: str | None = None
        self._failure = ""
        self._task: asyncio.Task[str] | None = None

    # ---- Talking to the clock ----

    def _api(self, key: str | None = None) -> DeskClockApi:
        assert self._host
        return DeskClockApi(async_get_clientsession(self.hass), self._host, key)

    async def _validate(self, host: str, api_key: str) -> tuple[dict[str, Any] | None, str | None]:
        """Returns (info, None) when the key works, else (None, error key)."""
        api = DeskClockApi(async_get_clientsession(self.hass), host, api_key)
        try:
            info = await api.info()
            if "id" not in info:
                _LOGGER.warning("The desk clock at %s runs firmware without a device ID; %s", host, REFLASH_HINT)
                return None, "firmware_too_old"
            if info.get("mode") != "normal":
                return None, "setup_mode"
            await api.config()
        except DeskClockAuthError:
            return None, "invalid_auth"
        except DeskClockError:
            return None, "cannot_connect"
        return info, None

    async def _create(self, host: str, info: dict[str, Any], key: str) -> ConfigFlowResult:
        """Stores a key that works: a new entry, or the new key for the entry being re-authenticated."""
        await self.async_set_unique_id(info["id"])
        if self.source == SOURCE_REAUTH:
            self._abort_if_unique_id_mismatch()
            return self.async_update_reload_and_abort(self._get_reauth_entry(), data_updates={CONF_API_KEY: key})
        self._abort_if_unique_id_configured(updates={CONF_HOST: host, CONF_API_KEY: key})
        return self.async_create_entry(title=_title(info), data={CONF_HOST: host, CONF_API_KEY: key})

    # ---- Entry points ----

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            try:
                info = await DeskClockApi(async_get_clientsession(self.hass), host).info()
            except DeskClockError:
                errors["base"] = "cannot_connect"
            else:
                if "id" not in info:
                    _LOGGER.warning("The desk clock at %s runs firmware without a device ID; %s", host, REFLASH_HINT)
                    errors["base"] = "firmware_too_old"
                elif info.get("mode") != "normal":
                    errors["base"] = "setup_mode"
                else:
                    await self.async_set_unique_id(info["id"])
                    self._abort_if_unique_id_configured(updates={CONF_HOST: host})
                    self._host, self._name = host, _title(info)
                    return await self._start(info)
        schema = vol.Schema({vol.Required(CONF_HOST): str})
        return self.async_show_form(
            step_id="user", data_schema=self.add_suggested_values_to_schema(schema, user_input), errors=errors
        )

    async def async_step_zeroconf(self, discovery_info: ZeroconfServiceInfo) -> ConfigFlowResult:
        # Discovery aborts never show in the UI, so each one that hides a clock says why in the log.
        if discovery_info.ip_address.version != 4:
            _LOGGER.debug("Ignoring desk clock IPv6 announcement %s", discovery_info.host)
            return self.async_abort(reason="not_ipv4")
        host = discovery_info.host if discovery_info.port in (None, 80) else f"{discovery_info.host}:{discovery_info.port}"
        device_id = discovery_info.properties.get("id")
        if not device_id:  # firmware from before the TXT record: ask the clock
            try:
                device_id = (await DeskClockApi(async_get_clientsession(self.hass), host).info()).get("id")
            except DeskClockError as err:
                _LOGGER.warning("Discovered a desk clock at %s but couldn't reach it: %s", host, err)
                return self.async_abort(reason="cannot_connect")
            if not device_id:
                _LOGGER.warning("Discovered a desk clock at %s, but its firmware has no device ID; %s", host, REFLASH_HINT)
                return self.async_abort(reason="firmware_too_old")
        await self.async_set_unique_id(device_id)
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})  # follows a new DHCP address
        self._host = host
        self._name = f"Desk Clock {discovery_info.hostname.split('.')[0].rsplit('-', 1)[-1].upper()}"
        self.context["title_placeholders"] = {"name": self._name}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        """Nothing is asked here. Pairing starts only when the user clicks Submit, so a discovered clock never
        pops a card up on its own."""
        assert self._host
        if user_input is not None:
            return await self.async_step_pair()
        return self.async_show_form(
            step_id="zeroconf_confirm",
            description_placeholders={"name": self._name or self._host, "host": self._host},
        )

    async def async_step_reauth(self, entry_data: Mapping[str, Any]) -> ConfigFlowResult:
        entry = self._get_reauth_entry()
        self._host, self._name = entry.data[CONF_HOST], entry.title
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            return await self.async_step_pair()
        return self.async_show_form(step_id="reauth_confirm", description_placeholders={"name": self._name})

    # ---- Pairing: the clock shows a code, the user presses BOOT ----

    async def _start(self, info: dict[str, Any]) -> ConfigFlowResult:
        """After the manual host step: pair if the firmware can, else ask for the key."""
        if info.get("pair"):
            return await self.async_step_zeroconf_confirm()
        return await self.async_step_key()

    async def async_step_pair(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if self._task is None:
            try:
                info = await self._api().info()
                if info.get("mode") != "normal":
                    return self._failed("The clock is in setup mode. Let it rejoin your WiFi first.")
                if not info.get("pair"):
                    return await self.async_step_key()  # firmware from before pairing
                started = await self._api().pair_start()
            except DeskClockError as err:
                return self._failed(f"Couldn't start pairing: {err}")
            self._code = started["code"]
            self._task = self.hass.async_create_task(self._wait_for_button(started["session"]))
        if not self._task.done():
            return self.async_show_progress(
                step_id="pair",
                progress_action="press_button",
                description_placeholders={"name": self._name or "", "code": self._code or ""},
                progress_task=self._task,
            )
        task, self._task = self._task, None
        try:
            self._key = task.result()
        except PairingFailed as err:
            self._failure = str(err)
            return self.async_show_progress_done(next_step_id="pair_failed")
        return self.async_show_progress_done(next_step_id="pair_done")

    async def _wait_for_button(self, session: str) -> str:
        """Polls until the clock hands over its key (BOOT pressed), or says why not."""
        loop = asyncio.get_running_loop()
        deadline = loop.time() + PAIR_GIVE_UP
        while loop.time() < deadline:
            await asyncio.sleep(PAIR_POLL_INTERVAL)
            try:
                reply = await self._api().pair_poll(session)
            except DeskClockConnectionError:
                continue  # a busy clock drops a request now and then
            except DeskClockError as err:
                raise PairingFailed("The clock no longer knows this request.") from err
            state = reply.get("state")
            if state == "approved" and reply.get("token"):
                return reply["token"]
            if state == "denied":
                raise PairingFailed("Pairing was cancelled on the clock.")
            if state == "expired":
                raise PairingFailed("BOOT wasn't pressed in time.")
        raise PairingFailed("The clock stopped answering.")

    async def async_step_pair_done(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        assert self._host and self._key
        info, error = await self._validate(self._host, self._key)
        if not info:
            return self._failed(f"The clock handed over a key that didn't work ({error}).")
        return await self._create(self._host, info, self._key)

    def _failed(self, reason: str) -> ConfigFlowResult:
        self._failure = reason
        return self.async_show_menu(
            step_id="pair_failed", menu_options=["pair", "key"], description_placeholders={"reason": reason}
        )

    async def async_step_pair_failed(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        return self._failed(self._failure)

    # ---- Fallback: type the key from the setup page ----

    async def async_step_key(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        assert self._host
        errors: dict[str, str] = {}
        if user_input is not None:
            key = user_input[CONF_API_KEY].strip()
            info, error = await self._validate(self._host, key)
            if info:
                return await self._create(self._host, info, key)
            errors["base"] = error
        return self.async_show_form(
            step_id="key",
            data_schema=KEY_SCHEMA,
            errors=errors,
            description_placeholders={"name": self._name or self._host},
        )
