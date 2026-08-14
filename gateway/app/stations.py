"""Station directory built from the MTA Subway Stations dataset snapshot.

Snapshot source: https://data.ny.gov/resource/39hk-dx4f.json
Regenerate with scripts/refresh_stations.py.
"""

import json
from pathlib import Path

_DATA_FILE = Path(__file__).parent / "data" / "stations.json"

_stations: list[dict] = []
_by_stop_id: dict[str, dict] = {}


def load() -> None:
    global _stations, _by_stop_id
    _stations = json.loads(_DATA_FILE.read_text())
    _by_stop_id = {s["stop_id"]: s for s in _stations}


def get(stop_id: str) -> dict | None:
    return _by_stop_id.get(stop_id)


def search(query: str, limit: int = 25) -> list[dict]:
    q = query.strip().lower()
    if not q:
        return _stations[:limit]
    hits = [s for s in _stations if q in s["name"].lower()]
    return hits[:limit]


def direction_label(stop_id: str, direction: str) -> str:
    station = _by_stop_id.get(stop_id)
    if not station:
        return "Northbound" if direction == "N" else "Southbound"
    return station["north"] if direction == "N" else station["south"]
