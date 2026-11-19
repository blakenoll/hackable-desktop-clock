"""Client for the clock's settings API (docs/API.md in the firmware repo)."""

from __future__ import annotations

import asyncio
from typing import Any

import aiohttp

from .const import REQUEST_TIMEOUT


class DeskClockError(Exception):
    """The clock answered with an error."""


class DeskClockConnectionError(DeskClockError):
    """The clock could not be reached."""


class DeskClockAuthError(DeskClockError):
    """The API key is missing or wrong."""


class DeskClockApi:
    """Talks to one clock over plain HTTP on the local network."""

    def __init__(self, session: aiohttp.ClientSession, host: str, api_key: str | None = None) -> None:
        self._session = session
        self.host = host
        self._api_key = api_key

    async def _request(
        self, method: str, path: str, body: dict[str, Any] | None = None, params: dict[str, str] | None = None
    ) -> dict[str, Any]:
        headers = {"Authorization": f"Bearer {self._api_key}"} if self._api_key else {}
        try:
            async with asyncio.timeout(REQUEST_TIMEOUT):
                async with self._session.request(
                    method, f"http://{self.host}{path}", headers=headers, json=body, params=params
                ) as resp:
                    if resp.status == 401:
                        raise DeskClockAuthError("wrong API key")
                    data = await resp.json(content_type=None)
                    if resp.status >= 400:
                        raise DeskClockError(data.get("error", f"HTTP {resp.status}"))
                    return data
        except (aiohttp.ClientError, TimeoutError, ValueError) as err:
            raise DeskClockConnectionError(f"{self.host}: {err or type(err).__name__}") from err

    async def info(self) -> dict[str, Any]:
        """Return {"api", "id", "version", "host", "mode", "auth"} (no key needed)."""
        return await self._request("GET", "/api/info")

    async def schema(self) -> dict[str, Any]:
        """Return every setting's type, default, range and choices (no key needed)."""
        return await self._request("GET", "/api/schema")

    async def config(self) -> dict[str, Any]:
        """Return the current settings."""
        return await self._request("GET", "/api/config")

    async def set(self, changes: dict[str, Any]) -> dict[str, Any]:
        """Change some settings; returns {"ok", "restart", "config"}."""
        return await self._request("POST", "/api/config", changes)

    async def notify(self, notification: dict[str, Any]) -> dict[str, Any]:
        """Show a notification card: {"title", "text", "level", "duration", "id"}; replaces the current one."""
        return await self._request("POST", "/api/notify", notification)

    async def clear_notification(self, notification_id: str | None = None) -> dict[str, Any]:
        """Remove the current card (only if its id matches, when one is given); returns {"ok", "cleared"}."""
        return await self._request("DELETE", "/api/notify", params={"id": notification_id} if notification_id else None)
