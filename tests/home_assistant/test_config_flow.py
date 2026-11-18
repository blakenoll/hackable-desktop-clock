"""Config flow: manual setup, zeroconf discovery, reauth."""

from ipaddress import ip_address

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


async def test_user_flow(hass, clock):
    result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    assert result["type"] is FlowResultType.FORM

    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HOST: HOST, CONF_API_KEY: "nope"})
    assert result["errors"] == {"base": "invalid_auth"}

    clock.mode = "setup"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HOST: HOST, CONF_API_KEY: KEY})
    assert result["errors"] == {"base": "setup_mode"}

    clock.mode = "normal"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_HOST: f" {HOST} ", CONF_API_KEY: KEY})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["title"] == "Desk Clock A1B2"
    assert result["data"] == {CONF_HOST: HOST, CONF_API_KEY: KEY}
    assert result["result"].unique_id == DEVICE_ID


async def test_user_flow_cannot_connect(hass, clock):
    clock.online = False
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}, data={CONF_HOST: HOST, CONF_API_KEY: KEY}
    )
    assert result["errors"] == {"base": "cannot_connect"}


async def test_zeroconf_flow(hass, clock):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=_zeroconf()
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "zeroconf_confirm"
    assert result["description_placeholders"]["name"] == "Desk Clock A1B2"

    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_API_KEY: KEY})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"] == {CONF_HOST: "192.168.1.50", CONF_API_KEY: KEY}


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
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {CONF_API_KEY: KEY})
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "reauth_successful"
    assert entry.data[CONF_API_KEY] == KEY
