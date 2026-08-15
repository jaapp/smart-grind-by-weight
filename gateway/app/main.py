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
from pydantic import BaseModel, Field

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
    walk_min: int | None = Field(None, ge=1, le=config.MAX_WALK_MIN)


class WatchUpdate(BaseModel):
    # Walk time to the platform in minutes; null clears the estimate
    walk_min: int | None = Field(None, ge=1, le=config.MAX_WALK_MIN)


def watch_view(w: dict) -> dict:
    station = stations.get(w["stop_id"])
    return {
        **w,
        "walk_min": w.get("walk_min"),
        "station": station["name"] if station else w["stop_id"],
        "direction_label": stations.direction_label(w["stop_id"], w["direction"]),
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
            "direction": stations.direction_label(w["stop_id"], w["direction"]),
            "walk_min": w.get("walk_min"),
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
    key = (watch.route, watch.stop_id, watch.direction)
    if any((w["route"], w["stop_id"], w["direction"]) == key for w in watches):
        raise HTTPException(409, "already watching")
    entry = watch.model_dump(exclude_none=True)
    watches.append(entry)
    config.save_watches(watches)
    cache.set_watched_routes({w["route"] for w in watches})
    await cache.refresh_now()
    return [watch_view(w) for w in watches]


@app.patch("/api/watches/{index}")
def update_watch(index: int, update: WatchUpdate) -> list[dict]:
    if not 0 <= index < len(watches):
        raise HTTPException(404, "no such watch")
    if update.walk_min is None:
        watches[index].pop("walk_min", None)
    else:
        watches[index]["walk_min"] = update.walk_min
    config.save_watches(watches)
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
