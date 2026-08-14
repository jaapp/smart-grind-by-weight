"""Watchlist persistence: a JSON file on the /data volume."""

import json
import os
from pathlib import Path

DATA_DIR = Path(os.environ.get("GATEWAY_DATA_DIR", "/data"))
CONFIG_FILE = DATA_DIR / "config.json"

MAX_WATCHES = 8
ARRIVALS_PER_WATCH = 4


def load_watches() -> list[dict]:
    try:
        return json.loads(CONFIG_FILE.read_text())["watches"]
    except (OSError, ValueError, KeyError):
        return []


def save_watches(watches: list[dict]) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    tmp = CONFIG_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps({"watches": watches}, indent=2) + "\n")
    tmp.replace(CONFIG_FILE)
