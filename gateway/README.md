# MTA Gateway

Small Docker service that polls the MTA's public subway GTFS-realtime feeds and
exposes a minimal arrivals list for the grinder's trains screensaver. The ESP32
can't parse protobuf feeds itself, so this runs on a desktop on the same network.

The image is published automatically to GHCR whenever `gateway/` changes on `main`.

## Run

```bash
docker run -d --name mta-gateway --restart unless-stopped \
  -p 8600:8600 -v mta-gateway:/data \
  ghcr.io/sebastienstdenis/mta-gateway:latest
```

Open `http://localhost:8600` to configure which trains to watch: search a
station, pick a line, pick a direction (shown with friendly labels like
"→ Manhattan"). Watches persist in the `/data` volume.

## API

`GET /api/arrivals` — what the grinder polls:

```json
{
  "age_s": 12,
  "stale": false,
  "items": [
    {"route": "N", "color": "FCCC0A", "text_color": "000000",
     "station": "Queensboro Plaza", "direction": "Manhattan", "mins": [3, 9, 15]}
  ]
}
```

`mins` are minutes until arrival at the watched stop (up to 4 per watch).
`stale` is true when the last successful MTA fetch is too old to trust.

Also: `GET /api/health`, `GET /api/stations?q=`, `GET/POST /api/watches`,
`DELETE /api/watches/{index}`.

## Development

```bash
cd gateway
uv venv --python 3.12 .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn app.main:app --port 8600 --reload
```

Station directory (`app/data/stations.json`) is a committed snapshot of the
[MTA Subway Stations dataset](https://data.ny.gov/Transportation/MTA-Subway-Stations/39hk-dx4f);
regenerate it with `python3 scripts/refresh_stations.py`.
