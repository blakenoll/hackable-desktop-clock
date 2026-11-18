"""Home Assistant serves the integration's icon from custom_components/deskclock/brand/."""

from pathlib import Path

from homeassistant.setup import async_setup_component

BRAND = Path(__file__).parents[2] / "custom_components" / "deskclock" / "brand"


async def test_icon_served_from_integration(hass, hass_client):
    assert await async_setup_component(hass, "brands", {})
    client = await hass_client()
    for image in ("icon.png", "icon@2x.png", "dark_logo.png"):  # the logo and dark variants fall back to the icon
        resp = await client.get(f"/api/brands/integration/deskclock/{image}")
        assert resp.status == 200, image
        body = await resp.read()
        assert body[:8] == b"\x89PNG\r\n\x1a\n"
        assert body == (BRAND / ("icon@2x.png" if "@2x" in image else "icon.png")).read_bytes()
