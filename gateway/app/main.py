"""MTA gateway: polls subway GTFS-realtime feeds and serves a minimal
arrivals list for the grinder's trains screensaver, plus a small web UI
for picking which route/station/direction combinations to watch.
"""

import logging
import time
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel

from . import config, mta, stations

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")

STATIC_DIR = Path(__file__).parent.parent / "static"

cache = mta.FeedCache()
watches: list[dict] = []


@asynccontextmanager
async def lifespan(app: FastAPI):
    global watches
    stations.load()
    watches = config.load_watches()
    cache.set_watched_routes({w["route"] for w in watches})
    await cache.start()
    yield
    await cache.stop()


app = FastAPI(title="MTA Gateway", lifespan=lifespan)


class Watch(BaseModel):
    route: str
    stop_id: str
    direction: str  # "N" or "S"


def resolve_direction_label(w: dict) -> str:
    """Dataset labels like "Manhattan" are kept; generic filler ("Southbound",
    "Outbound") is replaced with the live trips' terminal station, matching
    what NYC countdown clocks display."""
    label = stations.direction_label(w["stop_id"], w["direction"])
    if label not in stations.GENERIC_LABELS:
        return label
    terminal = cache.terminal_for(w["route"], w["stop_id"], w["direction"])
    if terminal:
        name = stations.stop_name(terminal)
        if name:
            return name
    return label


def watch_view(w: dict) -> dict:
    station = stations.get(w["stop_id"])
    return {
        **w,
        "station": station["name"] if station else w["stop_id"],
        "direction_label": resolve_direction_label(w),
    }


@app.get("/api/arrivals")
def arrivals() -> dict:
    items = []
    for w in watches:
        color, text_color = mta.ROUTE_COLORS.get(w["route"], ("808183", "FFFFFF"))
        station = stations.get(w["stop_id"])
        items.append({
            "route": w["route"],
            "color": color,
            "text_color": text_color,
            "station": station["name"] if station else w["stop_id"],
            "direction": resolve_direction_label(w),
            "mins": cache.upcoming_minutes(
                w["route"], w["stop_id"], w["direction"], config.ARRIVALS_PER_WATCH
            ),
        })
    age = int(time.time() - cache.last_success) if cache.last_success else -1
    return {"age_s": age, "stale": age < 0 or age > 3 * mta.FETCH_INTERVAL_S, "items": items}


@app.get("/api/health")
def health() -> dict:
    return {
        "ok": True,
        "watches": len(watches),
        "last_success_age_s": int(time.time() - cache.last_success) if cache.last_success else None,
        "last_error": cache.last_error,
    }


@app.get("/api/stations")
def station_search(q: str = "") -> list[dict]:
    return stations.search(q)


@app.get("/api/watches")
def list_watches() -> list[dict]:
    return [watch_view(w) for w in watches]


@app.post("/api/watches", status_code=201)
async def add_watch(watch: Watch) -> list[dict]:
    if watch.direction not in ("N", "S"):
        raise HTTPException(400, "direction must be N or S")
    station = stations.get(watch.stop_id)
    if station is None:
        raise HTTPException(400, f"unknown stop_id {watch.stop_id}")
    if watch.route not in station["routes"]:
        raise HTTPException(400, f"route {watch.route} does not stop at {station['name']}")
    if len(watches) >= config.MAX_WATCHES:
        raise HTTPException(400, f"limit of {config.MAX_WATCHES} watches reached")
    entry = watch.model_dump()
    if entry in watches:
        raise HTTPException(409, "already watching")
    watches.append(entry)
    config.save_watches(watches)
    cache.set_watched_routes({w["route"] for w in watches})
    await cache.refresh_now()
    return [watch_view(w) for w in watches]


@app.delete("/api/watches/{index}")
def delete_watch(index: int) -> list[dict]:
    if not 0 <= index < len(watches):
        raise HTTPException(404, "no such watch")
    watches.pop(index)
    config.save_watches(watches)
    cache.set_watched_routes({w["route"] for w in watches})
    return [watch_view(w) for w in watches]


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")
