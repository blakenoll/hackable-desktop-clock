"""A fake clock speaking the settings API (docs/API.md) through Home Assistant's aiohttp mock."""

from __future__ import annotations

import copy
import json
import re
from typing import Any

import pytest
from pytest_homeassistant_custom_component.test_util.aiohttp import AiohttpClientMockResponse

HOST = "deskclock-a1b2.local"
KEY = "0123456789abcdef"
DEVICE_ID = "a0b1c2d3a1b2"


def _choices(labels: list[str]) -> list[dict[str, Any]]:
    return [{"value": i, "label": label} for i, label in enumerate(labels)]


SCHEMA: dict[str, Any] = {
    "api": 1,
    "fields": [
        {"key": "ssid", "type": "text", "default": "", "restart": True},
        {"key": "pass", "type": "secret", "restart": True},
        {"key": "tz", "type": "text", "default": "PST8PDT,M3.2.0,M11.1.0", "restart": False,
         "choices": [{"value": "PST8PDT,M3.2.0,M11.1.0", "label": "US Pacific"}, {"value": "UTC0", "label": "UTC"}]},
        {"key": "city", "type": "text", "default": "", "restart": True},
        {"key": "units", "type": "choice", "default": "F", "restart": True,
         "choices": [{"value": "F", "label": "Fahrenheit"}, {"value": "C", "label": "Celsius"}]},
        {"key": "h24", "type": "bool", "default": False, "restart": False},
        {"key": "face", "type": "number", "default": 0, "min": 0, "max": 2, "restart": False,
         "choices": _choices(["Polished", "Arc", "Radar"])},
        {"key": "accent", "type": "number", "default": 0, "min": 0, "max": 1, "restart": False,
         "choices": _choices(["Red", "Mono"])},
        {"key": "nstart", "type": "number", "default": 22, "min": 0, "max": 23, "restart": False},
        {"key": "chime", "type": "bool", "default": True, "restart": False},
    ],
}


class FakeClock:
    """Holds settings, checks the Bearer key and validates POSTs like the firmware does."""

    def __init__(self, aioclient_mock) -> None:
        self.mock = aioclient_mock
        self.mode = "normal"
        self.key = KEY
        self.online = True
        self.version = "abc1234"
        self.schema = copy.deepcopy(SCHEMA)
        self.config: dict[str, Any] = {
            "ssid": "home", "tz": "PST8PDT,M3.2.0,M11.1.0", "city": "29401", "units": "F", "h24": False,
            "face": 0, "accent": 0, "nstart": 22, "chime": True,
        }
        for method, path in (("get", "info"), ("get", "schema"), ("get", "config"), ("post", "config")):
            aioclient_mock.request(method, re.compile(rf"/api/{path}$"), side_effect=self._handle)

    def _reply(self, method: str, url, status: int, body: dict[str, Any]) -> AiohttpClientMockResponse:
        return AiohttpClientMockResponse(method, url, status=status, text=json.dumps(body))

    async def _handle(self, method: str, url, data):
        if not self.online:
            from aiohttp import ClientConnectionError

            return AiohttpClientMockResponse(method, url, exc=ClientConnectionError("offline"))
        headers = self.mock.mock_calls[-1][3] or {}
        path = url.path
        if path == "/api/info":
            return self._reply(method, url, 200, {"api": 1, "id": DEVICE_ID, "version": self.version,
                                                  "host": "deskclock-a1b2", "mode": self.mode,
                                                  "auth": self.mode == "normal"})
        if path == "/api/schema":
            return self._reply(method, url, 200, self.schema)
        if self.mode == "normal" and headers.get("Authorization") != f"Bearer {self.key}":
            return self._reply(method, url, 401, {"error": "missing or wrong API key"})
        if method.lower() == "get":
            return self._reply(method, url, 200, self.config)
        fields = {f["key"]: f for f in self.schema["fields"]}
        restart = False
        for k, v in (data or {}).items():
            f = fields.get(k)
            if f is None:
                return self._reply(method, url, 400, {"error": f"{k}: unknown setting"})
            if f["type"] == "number" and not (f["min"] <= v <= f["max"]):
                return self._reply(method, url, 400, {"error": f"{k}: out of range"})
            if self.config.get(k) != v:
                restart |= f["restart"]
                self.config[k] = v
        return self._reply(method, url, 200, {"ok": True, "restart": restart, "config": self.config})


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    """Lets Home Assistant load custom_components/deskclock."""
    yield


@pytest.fixture
def clock(aioclient_mock) -> FakeClock:
    return FakeClock(aioclient_mock)
