#!/usr/bin/env python3
"""Regenerate app/data/stations.json from the MTA Subway Stations dataset."""

import json
import urllib.request
from pathlib import Path

SOURCE = (
    "https://data.ny.gov/resource/39hk-dx4f.json?$limit=1000"
    "&$select=gtfs_stop_id,stop_name,borough,daytime_routes,"
    "north_direction_label,south_direction_label"
)
TARGET = Path(__file__).parent.parent / "app" / "data" / "stations.json"


def main() -> None:
    with urllib.request.urlopen(SOURCE) as resp:
        rows = json.load(resp)
    out = [
        {
            "stop_id": r["gtfs_stop_id"],
            "name": r["stop_name"],
            "borough": r.get("borough", ""),
            "routes": r.get("daytime_routes", "").split(),
            "north": r.get("north_direction_label") or "Northbound",
            "south": r.get("south_direction_label") or "Southbound",
        }
        for r in rows
    ]
    out.sort(key=lambda s: s["stop_id"])
    TARGET.write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n")
    print(f"wrote {len(out)} stations to {TARGET}")


if __name__ == "__main__":
    main()
