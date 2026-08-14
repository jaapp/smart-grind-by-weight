"""MTA GTFS-realtime fetching and parsing.

The subway feeds are public (no API key) and grouped by line family.
Only the feeds needed by the current watchlist are fetched, on a fixed
interval, and parsed arrivals are served from cache.
"""

import asyncio
import logging
import time

import httpx
from google.transit import gtfs_realtime_pb2

logger = logging.getLogger("gateway.mta")

FEED_BASE = "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs"

_FEED_ROUTES: dict[str, set[str]] = {
    "": {"1", "2", "3", "4", "5", "6", "7", "S", "GS"},
    "-ace": {"A", "C", "E", "H", "SR"},
    "-bdfm": {"B", "D", "F", "M", "FS", "SF"},
    "-g": {"G"},
    "-jz": {"J", "Z"},
    "-l": {"L"},
    "-nqrw": {"N", "Q", "R", "W"},
    "-si": {"SI", "SIR"},
}

ROUTE_COLORS: dict[str, tuple[str, str]] = {
    **dict.fromkeys(["1", "2", "3"], ("EE352E", "FFFFFF")),
    **dict.fromkeys(["4", "5", "6"], ("00933C", "FFFFFF")),
    "7": ("B933AD", "FFFFFF"),
    **dict.fromkeys(["A", "C", "E"], ("0039A6", "FFFFFF")),
    **dict.fromkeys(["B", "D", "F", "M"], ("FF6319", "FFFFFF")),
    "G": ("6CBE45", "FFFFFF"),
    **dict.fromkeys(["J", "Z"], ("996633", "FFFFFF")),
    "L": ("A7A9AC", "FFFFFF"),
    **dict.fromkeys(["N", "Q", "R", "W"], ("FCCC0A", "000000")),
    **dict.fromkeys(["S", "GS", "FS", "SF", "H", "SR"], ("808183", "FFFFFF")),
    **dict.fromkeys(["SI", "SIR"], ("0039A6", "FFFFFF")),
}

FETCH_INTERVAL_S = 30
REQUEST_TIMEOUT_S = 15


def feed_for_route(route: str) -> str | None:
    for suffix, routes in _FEED_ROUTES.items():
        if route in routes:
            return suffix
    return None


class FeedCache:
    """Cached arrivals per (route, directional stop id), refreshed in the background."""

    def __init__(self) -> None:
        # (route, stop_id + direction) -> sorted list of epoch arrival times
        self.arrivals: dict[tuple[str, str], list[int]] = {}
        self.last_success: float = 0.0
        self.last_error: str | None = None
        self._task: asyncio.Task | None = None
        self._watched_feeds: set[str] = set()

    def set_watched_routes(self, routes: set[str]) -> None:
        feeds = {f for r in routes if (f := feed_for_route(r)) is not None}
        self._watched_feeds = feeds

    async def start(self) -> None:
        self._task = asyncio.create_task(self._poll_loop())

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()

    async def refresh_now(self) -> None:
        await self._refresh()

    async def _poll_loop(self) -> None:
        while True:
            try:
                await self._refresh()
            except Exception:
                logger.exception("feed refresh failed")
            await asyncio.sleep(FETCH_INTERVAL_S)

    async def _refresh(self) -> None:
        if not self._watched_feeds:
            return
        arrivals: dict[tuple[str, str], list[int]] = {}
        async with httpx.AsyncClient(timeout=REQUEST_TIMEOUT_S) as client:
            results = await asyncio.gather(
                *(self._fetch_feed(client, f) for f in sorted(self._watched_feeds)),
                return_exceptions=True,
            )
        ok = False
        for result in results:
            if isinstance(result, BaseException):
                self.last_error = str(result)
                logger.warning("feed fetch error: %s", result)
                continue
            ok = True
            for key, times in result.items():
                arrivals.setdefault(key, []).extend(times)
        if ok:
            self.arrivals = {k: sorted(v) for k, v in arrivals.items()}
            self.last_success = time.time()
            self.last_error = None

    async def _fetch_feed(
        self, client: httpx.AsyncClient, suffix: str
    ) -> dict[tuple[str, str], list[int]]:
        resp = await client.get(FEED_BASE + suffix)
        resp.raise_for_status()
        feed = gtfs_realtime_pb2.FeedMessage()
        feed.ParseFromString(resp.content)

        out: dict[tuple[str, str], list[int]] = {}
        for entity in feed.entity:
            if not entity.HasField("trip_update"):
                continue
            route = entity.trip_update.trip.route_id
            for stu in entity.trip_update.stop_time_update:
                event = stu.arrival if stu.HasField("arrival") else stu.departure
                if not event.time:
                    continue
                out.setdefault((route, stu.stop_id), []).append(event.time)
        return out

    def upcoming_minutes(
        self, route: str, stop_id: str, direction: str, limit: int
    ) -> list[int]:
        now = time.time()
        times = self.arrivals.get((route, stop_id + direction), [])
        mins = [int((t - now) // 60) for t in times if t >= now - 15]
        return [max(m, 0) for m in mins if m <= 120][:limit]
