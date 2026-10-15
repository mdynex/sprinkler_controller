# Sprinkler Controller — AI Interface Guide

You are helping control an Arduino-based sprinkler system over a local HTTP REST API.
Ask the user for their board's IP address if you don't have it. All requests go to
`http://<IP>/<path>` on port 80. All responses are JSON.

There are **8 zones** (ids 1–8). Zones run one at a time; only one job can run at once.

The controller stores up to **8 jobs**, and they survive a power cycle. A job is
both concepts in one:

- `"repeat": "none"` → a **run command**: fires once at `run_at`
- `"repeat": "daily"` / `"weekly"` → a **schedule**: fires at `run_time` on the
  days in `days`

`on_complete` says what happens afterwards: `"delete"` (retire it — the default
for one-shots), `"disable"` (keep it listed but switched off), or `"keep"`.

To turn a run command into a schedule, `PUT` a `repeat` onto it. Same id, no
recreation. Don't delete and recreate — you'd lose the id the server mirrors.

---

## Quick reference

| Action | Method | Path |
|--------|--------|------|
| Get current time | GET | `/time` |
| List all zones | GET | `/zones` |
| Turn a zone on | POST | `/zones/{id}/on` |
| Turn a zone off | POST | `/zones/{id}/off` |
| Set zone watering rate | PUT | `/zones/{id}` |
| Send a generic command | POST | `/command` |
| List all jobs | GET | `/jobs` |
| Get one job | GET | `/jobs/{id}` |
| Add a job | POST | `/jobs` |
| Update a job (partial) | PUT | `/jobs/{id}` |
| Delete a job | DELETE | `/jobs/{id}` |
| Run a job now | POST | `/jobs/{id}/run` |
| Stop everything | POST | `/jobs/stop` |
| Check what's running | GET | `/jobs/run` |
| What was missed while off | GET | `/jobs/missed` |

The old `/schedules...` paths are aliases for the `/jobs...` ones and still work.

**`POST /jobs` adds** — it does not replace the existing jobs. (The pre-job
firmware held a single schedule and replaced it on every POST; that is no
longer true.) It returns `507` when all 8 slots are full.

A step with `"zone": 0` is an **idle/soak pause** — no valve opens for its
duration. Use it to represent gaps in a cycle-and-soak plan.

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

## Jobs

### GET /jobs
```json
[
  {
    "id": 1,
    "name": "Morning",
    "origin": "server",
    "enabled": true,
    "repeat": "weekly",
    "on_complete": "keep",
    "auto_run": true,
    "run_time": "06:00",
    "run_at": 0,
    "last_run": 1755237600,
    "running": false,
    "transient": false,
    "days": [1, 3, 5],
    "zones": [
      {"zone": 1, "duration": 300},
      {"zone": 0, "duration": 600},
      {"zone": 3, "duration": 600}
    ]
  }
]
```

### POST /jobs — add
### PUT /jobs/{id} — update (partial: only the keys you send change)

A one-shot run command for a specific moment:
```json
{
  "name": "Wednesday watering",
  "repeat": "none",
  "run_at": 1755237600,
  "on_complete": "delete",
  "zones": [{"zone": 1, "duration": 300}]
}
```

A recurring schedule:
```json
{
  "name": "Morning",
  "repeat": "weekly",
  "days": [1, 3, 5],
  "run_time": "06:30",
  "on_complete": "keep",
  "zones": [
    {"zone": 1, "duration": 300},
    {"zone": 0, "duration": 600},
    {"zone": 3, "duration": 600}
  ]
}
```

Promoting the run command above into a schedule — nothing else needed:
```json
{"repeat": "weekly", "days": [1, 3, 5], "run_time": "06:00"}
```

| Field | Required | Notes |
|-------|----------|-------|
| `name` | on create | max 31 characters |
| `repeat` | no | `"none"` (default), `"daily"`, `"weekly"` |
| `days` | no | `0`=Sun … `6`=Sat; supplying it implies `"weekly"` |
| `run_time` | no | 24-hour `"HH:MM"` for recurring jobs, default `"06:00"` |
| `run_at` | no | Unix epoch; when a one-shot fires |
| `on_complete` | no | `"keep"`, `"delete"`, `"disable"` |
| `enabled` | no | `false` keeps it listed but stops it firing |
| `zones` | on create | ordered list; steps run one at a time |
| `zones[].zone` | yes | zone id 1–8, or **`0` for an idle/soak pause** |
| `zones[].duration` | yes | seconds |

Durations are in **seconds** (e.g. 5 minutes = 300, 10 minutes = 600).

`auto_run` is derived (`repeat != "none" && enabled`) and read-only — it exists
for older clients. Set `repeat` instead.

If you change `repeat` without also sending `on_complete`, the device resets
`on_complete` to the sensible default for the new shape. That stops a promoted
run command from deleting itself after its first run.

### DELETE /jobs/{id}
```json
{"deleted": true}
```

### POST /jobs/{id}/run
```json
{"started": true, "job_id": 1, "schedule_id": 1}
```

### POST /jobs/stop
```json
{"stopped": true}
```

### GET /jobs/missed — what was missed while powered off
```json
[{"job_id": 4, "name": "Wed watering", "scheduled_at": 1755237600, "deleted": true}]
```
`deleted: true` means it was a stale one-shot and the device removed it.
Recurring schedules are reported but stay armed.

### GET /jobs/run — check what's running
Idle:
```json
{"running": false}
```
Active:
```json
{
  "running": true,
  "job_id": 1,
  "schedule_id": 1,
  "step": 2,
  "zone": 3,
  "idle": false,
  "elapsed_sec": 45,
  "duration_sec": 300
}
```
`idle: true` means the current step is a soak pause, not watering.

---

## Time

### GET /time
```json
{"synced": true, "time": "06:30:00", "epoch": 1715000000}
```

---

## Key constraints

- Zone ids are **1–8**; zone `0` in a step list means an idle/soak pause
- Only **one job runs at a time** — starting a new one stops the current one
- The controller holds at most **8 jobs**; `POST /jobs` returns `507` when full
- Jobs are **saved to flash** and survive a power cycle
- A recurring job fires once per day; the board won't re-run one that already ran in the last 23 hours
- A job missed while the board was off is either run late (if under 30 minutes)
  or reported to `/jobs/missed` — where a one-shot is deleted and a recurring
  schedule stays armed
- The board is on a **local network only** — no internet access required or supported
- Time is synced from NTP on boot; timezone is pre-configured on the board

---

## Tips for common requests

**"Water zone 3 for 10 minutes"** → `POST /command` with
`{"action":"zone_on","zone_id":3,"duration":600}`. It closes the valve on its
own, so nothing is left running if you lose contact with the board.

**"Water once tomorrow at 6am"** → `POST /jobs` with `"repeat":"none"`,
`"run_at"` set to tomorrow 06:00, and `"on_complete":"delete"`. It fires once
and removes itself.

**"Set up a morning schedule"** → `POST /jobs` with `"repeat":"weekly"`,
`"days"`, and the desired `"run_time"`.

**"Make that one-off water every Monday"** → `PUT /jobs/{id}` with
`{"repeat":"weekly","days":[1]}`. Don't delete and recreate it.

**"Turn off my morning schedule for now"** → `PUT /jobs/{id}` with
`{"enabled": false}`. It stays in the list and can be switched back on.

**"How long until zone 2 finishes?"** → `GET /jobs/run`, subtract `elapsed_sec` from `duration_sec`.

**"Stop everything"** → `POST /jobs/stop` (also turns off all zones).
