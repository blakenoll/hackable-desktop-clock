"""Config flow: manual setup, zeroconf discovery, reauth."""

from ipaddress import ip_address
from unittest.mock import patch

import pytest

from homeassistant import config_entries
from homeassistant.const import CONF_API_KEY, CONF_HOST
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.deskclock.const import DOMAIN

from .conftest import DEVICE_ID, HOST, KEY


def _zeroconf(ip: str = "192.168.1.50", props: dict | None = None) -> ZeroconfServiceInfo:
    return ZeroconfServiceInfo(
        ip_address=ip_address(ip),
        ip_addresses=[ip_address(ip)],
        hostname="deskclock-a1b2.local.",
        name="deskclock-a1b2._deskclock._tcp.local.",
        port=80,
        type="_deskclock._tcp.local.",
        properties={"api": "1", "id": DEVICE_ID} if props is None else props,
    )


@pytest.fixture(autouse=True)
def fast_polling():
    with patch("custom_components.deskclock.config_flow.PAIR_POLL_INTERVAL", 0):
        yield


async def _press_boot_then_finish(hass, clock, flow_id, approve: bool = True):
    """Starts pairing from a flow sitting on its confirm form, presses BOOT, and returns the final result."""
    result = await hass.config_entries.flow.async_configure(flow_id, {})
    assert result["type"] is FlowResultType.SHOW_PROGRESS
    assert result["progress_action"] == "press_button"
    assert result["description_placeholders"]["code"] == clock.pair_code
    assert clock.pair_state == "pending"
    clock.press_boot(approve)
    await hass.async_block_till_done()
    return await hass.config_entries.flow.async_configure(flow_id)  # Home Assistant has already stepped past "done"


async def test_user_flow_pairs(hass, clock):
    result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    assert result["type"] is FlowResultType.FORM

    clock.mode = "setup"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HOST: HOST})
    assert result["errors"] == {"base": "setup_mode"}

    clock.mode = "normal"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HOST: f" {HOST} "})
    assert result["step_id"] == "zeroconf_confirm"
    assert clock.pair_state is None  # nothing shows on the clock until the user clicks Submit

    result = await _press_boot_then_finish(hass, clock, result["flow_id"])
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["title"] == "Desk Clock A1B2"
    assert result["data"] == {CONF_HOST: HOST, CONF_API_KEY: KEY}
    assert result["result"].unique_id == DEVICE_ID


async def test_user_flow_cannot_connect(hass, clock):
    clock.online = False
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}, data={CONF_HOST: HOST}
    )
    assert result["errors"] == {"base": "cannot_connect"}


async def test_user_flow_old_firmware_asks_for_the_key(hass, clock):
    clock.pair_support = False
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}, data={CONF_HOST: HOST}
    )
    assert result["step_id"] == "key"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_API_KEY: "nope"})
    assert result["errors"] == {"base": "invalid_auth"}
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_API_KEY: KEY})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"] == {CONF_HOST: HOST, CONF_API_KEY: KEY}


async def test_zeroconf_flow(hass, clock):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "zeroconf_confirm"
    assert result["description_placeholders"]["name"] == "Desk Clock A1B2"
    assert clock.pair_state is None

    result = await _press_boot_then_finish(hass, clock, result["flow_id"])
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"] == {CONF_HOST: "192.168.1.50", CONF_API_KEY: KEY}


async def test_pairing_cancelled_on_the_clock_can_retry_or_type_the_key(hass, clock):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    result = await _press_boot_then_finish(hass, clock, result["flow_id"], approve=False)
    assert result["type"] is FlowResultType.MENU
    assert result["step_id"] == "pair_failed"
    assert "cancelled" in result["description_placeholders"]["reason"]

    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "pair"})
    assert result["type"] is FlowResultType.SHOW_PROGRESS  # a second try starts a fresh request
    clock.press_boot(True)
    await hass.async_block_till_done()
    result = await hass.config_entries.flow.async_configure(result["flow_id"])
    assert result["type"] is FlowResultType.CREATE_ENTRY


async def test_pairing_falls_back_to_typing_the_key(hass, clock):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    result = await _press_boot_then_finish(hass, clock, result["flow_id"], approve=False)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "key"})
    assert result["step_id"] == "key"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_API_KEY: KEY})
    assert result["type"] is FlowResultType.CREATE_ENTRY


async def test_pairing_busy_clock(hass, clock):
    clock.pair_state = "pending"  # someone else's request is open
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is FlowResultType.MENU
    assert "already in progress" in result["description_placeholders"]["reason"]


async def test_pairing_times_out_on_the_clock(hass, clock):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is FlowResultType.SHOW_PROGRESS
    clock.pair_state = "expired"
    await hass.async_block_till_done()
    result = await hass.config_entries.flow.async_configure(result["flow_id"])
    assert result["type"] is FlowResultType.MENU
    assert "BOOT wasn't pressed" in result["description_placeholders"]["reason"]


async def test_zeroconf_without_txt_id_asks_the_clock(hass, clock):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf(props={"api": "1"})
    )
    assert result["step_id"] == "zeroconf_confirm"
    flow = hass.config_entries.flow.async_get(result["flow_id"])
    assert flow["context"]["unique_id"] == DEVICE_ID


async def test_zeroconf_updates_host_of_known_clock(hass, clock):
    entry = MockConfigEntry(domain=DOMAIN, unique_id=DEVICE_ID, data={CONF_HOST: "192.168.1.9", CONF_API_KEY: KEY})
    entry.add_to_hass(hass)
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "already_configured"
    assert entry.data[CONF_HOST] == "192.168.1.50"


async def test_reauth(hass, clock):
    entry = MockConfigEntry(domain=DOMAIN, unique_id=DEVICE_ID, data={CONF_HOST: HOST, CONF_API_KEY: "old"})
    entry.add_to_hass(hass)
    result = await entry.start_reauth_flow(hass)
    assert result["step_id"] == "reauth_confirm"
    result = await _press_boot_then_finish(hass, clock, result["flow_id"])
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "reauth_successful"
    assert entry.data[CONF_API_KEY] == KEY
    await hass.async_block_till_done()  # the reload that reauth triggers must finish before teardown


async def test_old_firmware(hass, clock, caplog):
    clock.old_firmware = True
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf(props={"api": "1"})
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "firmware_too_old"
    assert "reflash" in caplog.text

    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}, data={CONF_HOST: HOST}
    )
    assert result["errors"] == {"base": "firmware_too_old"}


async def test_zeroconf_unreachable_is_logged(hass, clock, caplog):
    clock.online = False
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf(props={"api": "1"})
    )
    assert result["reason"] == "cannot_connect"
    assert "couldn't reach it" in caplog.text
