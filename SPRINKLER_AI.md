# Sprinkler Controller — AI Interface Guide

You are helping control an Arduino-based sprinkler system over a local HTTP REST API.
Ask the user for their board's IP address if you don't have it. All requests go to
`http://<IP>/<path>` on port 80. All responses are JSON.

There are **8 zones** (ids 1–8). Zones run one at a time; only one schedule can run at once.

---

## Quick reference

| Action | Method | Path |
|--------|--------|------|
| Get current time | GET | `/time` |
| List all zones | GET | `/zones` |
| Turn a zone on | POST | `/zones/{id}/on` |
| Turn a zone off | POST | `/zones/{id}/off` |
| Set zone watering rate | PUT | `/zones/{id}` |
| List all schedules | GET | `/schedules` |
| Get one schedule | GET | `/schedules/{id}` |
| Create a schedule | POST | `/schedules` |
| Update a schedule | PUT | `/schedules/{id}` |
| Delete a schedule | DELETE | `/schedules/{id}` |
| Run a schedule now | POST | `/schedules/{id}/run` |
| Stop everything | POST | `/schedules/stop` |
| Check what's running | GET | `/schedules/run` |

---

## Zones

### GET /zones
```json
[
  {"id": 1, "enabled": true,  "state": "off", "rate": 1.0},
  {"id": 2, "enabled": false, "state": "off", "rate": 1.5}
]
```

### POST /zones/{id}/on — POST /zones/{id}/off
No body required.
```json
{"id": 2, "state": "on"}
```

### PUT /zones/{id} — update zone settings
Either or both fields may be included in one request.
```json
{"enabled": false, "rate": 1.5}
```
- `enabled`: `true` / `false` — disabled zones are skipped by the scheduler and cannot be toggled
- `rate`: inches per hour, **0.1 – 5.0**, one decimal place
```json
{"id": 2, "enabled": false, "rate": 1.5}
```

---

## Schedules

### GET /schedules
```json
[
  {
    "id": 1,
    "name": "Morning",
    "auto_run": true,
    "run_time": "06:00",
    "zones": [
      {"zone": 1, "duration": 300},
      {"zone": 3, "duration": 600}
    ]
  }
]
```

### POST /schedules — create
### PUT /schedules/{id} — update (same body)

```json
{
  "name": "Morning",
  "auto_run": true,
  "run_time": "06:30",
  "zones": [
    {"zone": 1, "duration": 300},
    {"zone": 3, "duration": 600}
  ]
}
```

| Field | Required | Notes |
|-------|----------|-------|
| `name` | yes | max 31 characters |
| `auto_run` | no | `true` = runs daily at `run_time` |
| `run_time` | no | 24-hour `"HH:MM"`, default `"06:00"` |
| `zones` | yes | ordered list; zones run one at a time in this order |
| `zones[].zone` | yes | zone id 1–8 |
| `zones[].duration` | yes | seconds to run this zone |

Durations are in **seconds** (e.g. 5 minutes = 300, 10 minutes = 600).

### DELETE /schedules/{id}
```json
{"deleted": true}
```

### POST /schedules/{id}/run
```json
{"started": true, "schedule_id": 1}
```

### POST /schedules/stop
```json
{"stopped": true}
```

### GET /schedules/run — check what's running
Idle:
```json
{"running": false}
```
Active:
```json
{
  "running": true,
  "schedule_id": 1,
  "step": 2,
  "zone": 3,
  "elapsed_sec": 45,
  "duration_sec": 300
}
```

---

## Time

### GET /time
```json
{"synced": true, "time": "06:30:00", "epoch": 1715000000}
```

---

## Key constraints

- Zone ids are **1–8**
- Only **one schedule runs at a time** — starting a new one stops the current one
- Schedules are stored in **RAM only** and are lost on power cycle
- `auto_run` fires once per day; the board won't re-run a schedule that already ran in the last 23 hours
- The board is on a **local network only** — no internet access required or supported
- Time is synced from NTP on boot; timezone is pre-configured on the board

---

## Tips for common requests

**"Water zone 3 for 10 minutes"** → `POST /zones/3/on`, then `POST /zones/3/off` after 10 minutes — or create a one-zone schedule and run it.

**"Set up a morning schedule"** → `POST /schedules` with `auto_run: true` and the desired `run_time`.

**"How long until zone 2 finishes?"** → `GET /schedules/run`, subtract `elapsed_sec` from `duration_sec`.

**"Stop everything"** → `POST /schedules/stop` (also turns off all zones).
