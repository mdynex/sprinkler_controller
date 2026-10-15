# Sprinkler Controller

An Arduino-based sprinkler controller with a touchscreen UI and a WiFi REST API. Control your sprinkler zones from the display or from any app or home automation system on your network.

---

## Hardware

| Part | Purpose |
|------|---------|
| Arduino Giga R1 WiFi | Main microcontroller |
| Arduino Giga Display Shield | 800×480 touchscreen UI |
| 5V 8-Channel Relay Module (LOW-TRIGGER) | Switches sprinkler valves |

> **Important:** The Arduino Giga R1 WiFi supports **2.4 GHz only**. It cannot connect to 5 GHz or 6 GHz networks. If your router broadcasts all bands under the same name, give your 2.4 GHz band its own name in your router settings.

---

## Wiring

Connect the relay module to the Giga R1:

| Relay pin | Giga R1 pin |
|-----------|-------------|
| VCC | 5V |
| GND | GND |
| IN1 | D2 (Zone 1) |
| IN2 | D3 (Zone 2) |
| IN3 | D4 (Zone 3) |
| IN4 | D5 (Zone 4) |
| IN5 | D6 (Zone 5) |
| IN6 | D7 (Zone 6) |
| IN7 | D8 (Zone 7) |
| IN8 | D9 (Zone 8) |

Zone-to-pin assignments can be changed in `config.h`.

---

## Setup

### 1. Install Arduino board support

In Arduino IDE go to **Tools → Board → Boards Manager**, search for **Arduino Mbed OS Giga Boards**, and install it.

### 2. Install libraries

Go to **Sketch → Include Library → Manage Libraries** and install:

- `ArduinoJson` by Benoit Blanchon
- `Arduino_GigaDisplay_GFX`
- `Arduino_GigaDisplayTouch`

### 3. Configure WiFi and pins

Copy `config_example.h` to `config.h` and edit it:

```cpp
const char* WIFI_SSID     = "Your_2.4GHz_Network";
const char* WIFI_PASSWORD = "yourpassword";

const long UTC_OFFSET_SECONDS = -21600;  // change to your timezone

const int ZONE_PINS[ZONE_COUNT] = {2, 3, 4, 5, 6, 7, 8, 9};  // change if needed
```

> `config.h` is in `.gitignore` and will never be committed — your credentials stay private.

### 4. Upload

Select **Tools → Board → Arduino Giga R1 WiFi**, then click Upload.

The display will show **"Connecting…"** while joining WiFi, then **"Connected!"** with the board's IP address. After that the home screen loads.

---

## Touchscreen UI

### Home screen
Shows all 8 zones as buttons. **Tap any zone** to toggle it on or off instantly. The status bar at the bottom shows the currently running job.

- **SCHEDULES** — navigate to the job list
- **STOP ALL** — immediately stop whatever is running and turn all zones off

### Jobs screen
Lists every stored job, run commands and schedules together — they are the same
kind of thing, and the row tells you which it is. Each row shows the name, when
it next runs (`Once 06:00`, `Daily 06:00`, or `MoWeFr 06:00`), whether it
retires after running, and how many steps it has. Pages 4 at a time.

- **Tap RUN / STOP** on the right of a row to start or stop that job
- **Tap ON / OFF** to switch a job off without deleting it — it stays listed but never fires
- **Tap anywhere else on a row** to open the Edit screen for that job
- **+ NEW** — build a new job on the device
- **&lt; / &gt;** — page through the list when there are more than 4
- **BACK** — return to the home screen
- **STOP ALL** — stop everything

### Edit screen
Configure one job's timing.

- **Step list** — the job's zones in order, paginated. A `Soak` entry is an idle pause.
- **Day toggles** — this is the run-command/schedule switch. **Select no days and the job runs once and retires; select any day and it repeats weekly.**
- **Time controls** — set the hour (1-hour steps) and minute (5-minute steps)
- **CANCEL** — go back without saving
- **SAVE** — save changes and go back
- **DELETE** — remove the job from the controller
- **RUN NOW** — save changes and start the job immediately

Jobs pushed from the app show a **FROM APP** badge; ones built here show
**ON-DEVICE**. Step lists from the server aren't editable on the display —
edit those in the app.

---

## REST API

The board hosts an HTTP server on port 80. Find the board's IP address on the display or in the Serial Monitor (115200 baud) after boot.

All responses are JSON. Replace `<IP>` with your board's address (e.g. `192.168.1.42`).

---

### Time

#### `GET /time`
Returns the current synced time.

```
GET http://<IP>/time
```
```json
{"synced": true, "time": "06:30:00", "epoch": 1715000000}
```

---

### Zones

#### `GET /zones`
List all zones and their current state.

```
GET http://<IP>/zones
```
```json
[
  {"id": 1, "state": "off"},
  {"id": 2, "state": "on"}
]
```

#### `POST /zones/{id}/on` and `POST /zones/{id}/off`
Turn a zone on or off. `id` is 1–8.

```
POST http://<IP>/zones/2/on
```
```json
{"id": 2, "state": "on"}
```

---

### `POST /command`

The generic device-command endpoint the Home AI Server's rules engine and
time-based actions post to. It translates a command onto the job machinery
rather than being a separate code path.

```
POST http://<IP>/command
Content-Type: application/json
```

| Body | Effect |
|------|--------|
| `{"action":"zone_on","zone_id":2,"duration":300}` | Runs zone 2 for 5 min as a self-deleting one-shot |
| `{"action":"zone_on","zone_id":2}` | Opens zone 2 and leaves it open |
| `{"action":"zone_off","zone_id":2}` | Closes zone 2 |
| `{"action":"stop"}` | Stops everything |
| `{"action":"run_job","job_id":3}` | Starts job 3 immediately |

A command **with** a duration becomes a job so the runner closes the valve even
if the server never calls back. Those jobs are marked `transient` — they live
in RAM only and are never written to flash, so a rule firing on an interval
doesn't wear the flash out.

An unknown action returns `400` with the action echoed back, so a misconfigured
rule is diagnosable from the server log.

---

### Jobs

A **job** is the single entity behind both things you'd normally think of
separately:

| You want | `repeat` | `on_complete` |
|----------|----------|---------------|
| Run once, then it's gone | `"none"` | `"delete"` |
| Run once, keep it listed but switched off | `"none"` | `"disable"` |
| Repeat, but turn off after the next run | `"daily"` / `"weekly"` | `"disable"` |
| Normal recurring schedule | `"daily"` / `"weekly"` | `"keep"` |

Promoting a run command into a schedule is a one-field change — `PUT` a
`repeat` onto it. The id stays the same and nothing is recreated.

The controller holds up to **8 jobs**, and they survive a power cycle.

> The older `/schedules...` paths are kept as aliases for every route below, so
> existing clients keep working.

#### `GET /jobs`
List every stored job.

```
GET http://<IP>/jobs
```
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

#### `POST /jobs`
Add a job. **This does not replace existing jobs** — it adds to the list.
Returns `507` when all 8 slots are full.

```
POST http://<IP>/jobs
Content-Type: application/json
```
```json
{
  "name": "Wednesday watering",
  "repeat": "none",
  "run_at": 1755237600,
  "on_complete": "delete",
  "zones": [
    {"zone": 1, "duration": 300},
    {"zone": 0, "duration": 600},
    {"zone": 3, "duration": 300}
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Label for this job (max 31 chars) |
| `repeat` | string | no | `"none"` (default), `"daily"`, `"weekly"` |
| `days` | array | no | `0`=Sun … `6`=Sat. Supplying this implies `"weekly"` |
| `run_time` | string | no | 24-hour `"HH:MM"` for recurring jobs, default `"06:00"` |
| `run_at` | number | no | Unix epoch for a one-shot's fire time |
| `on_complete` | string | no | `"keep"`, `"delete"`, `"disable"` |
| `enabled` | boolean | no | `false` keeps it listed but stops it firing |
| `zones` | array | yes | Ordered list of steps |
| `zones[].zone` | number | yes | Zone `1`–`8`, or **`0` for an idle/soak pause** |
| `zones[].duration` | number | yes | Seconds |

Steps run one at a time in the order listed. A step with `"zone": 0` opens no
valve — it's a soak pause, which is what lets a cycle-and-soak plan survive
the trip from the server.

`auto_run` is still emitted for older clients; it's derived
(`repeat != "none" && enabled`), never stored.

#### `GET /jobs/{id}`
Get one job by id.

#### `PUT /jobs/{id}`
Partial update — only the keys you send are changed. This is how a run command
becomes a schedule:

```
PUT http://<IP>/jobs/1
Content-Type: application/json
```
```json
{"repeat": "weekly", "days": [1, 3, 5], "run_time": "06:00"}
```

Changing `repeat` without also sending `on_complete` resets it to the sensible
default for the new shape (`keep` for recurring, `delete` for one-shot), so a
promoted run command doesn't delete itself after its first run.

#### `DELETE /jobs/{id}`
Delete a job. If it is currently running it is stopped first.

```json
{"deleted": true}
```

#### `POST /jobs/{id}/run`
Start a job immediately.

```json
{"started": true, "job_id": 1, "schedule_id": 1}
```

#### `POST /jobs/stop`
Stop whatever is running and turn all zones off.

```json
{"stopped": true}
```

#### `GET /jobs/missed`
Jobs whose scheduled time passed while the controller was powered off.

The device writes a liveness marker every 5 minutes; at boot, anything due
inside the gap was missed. If it's less than 30 minutes late it simply runs
late instead (so a watchdog reset doesn't drop that day's watering). Beyond
that it is reported here — and a **one-shot is deleted** (a run command for a
time that has passed is stale) while a **recurring schedule stays armed**.

These are also pushed to the server on the next heartbeat, which is what turns
them into a notification in the app.

```json
[{"job_id": 4, "name": "Wed watering", "scheduled_at": 1755237600, "deleted": true}]
```

#### `GET /jobs/run`
Check what is currently running.

```
GET http://<IP>/jobs/run
```

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

## How auto-scheduling works

The board syncs time from `pool.ntp.org` on startup and re-syncs every hour.
Every 30 seconds it checks whether any enabled job is due:

- a **recurring** job fires when `run_time` matches the current hour and minute
  on a day in its `days` mask, and it hasn't run in the last 23 hours
- a **one-shot** fires once the clock passes its `run_at`

Only one job runs at a time — zones are physically exclusive. A job that comes
due while another is running is retried on later checks.

When a job finishes, its `on_complete` decides what happens to it: `delete`
removes it, `disable` leaves it listed but switched off, `keep` leaves it armed.

All of this can also be configured from the display without using the API.

---

## Notes

- Jobs are saved to on-board flash and survive a power cycle. Anything the
  board slept through is reconciled at boot — see `GET /jobs/missed`.
- The relay module is **LOW-TRIGGER**: the board pulls a pin LOW to open a valve and HIGH to close it. All relays are set to off at startup.
- The API has no authentication — anyone on your local network can control the sprinklers. Do not expose the board's port to the internet.
