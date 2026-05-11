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
Shows all 8 zones as buttons. **Tap any zone** to toggle it on or off instantly. The status bar at the bottom shows the currently running schedule.

- **SCHEDULES** — navigate to the schedule list
- **STOP ALL** — immediately stop any running schedule and turn all zones off

### Schedules screen
Lists all saved schedules. Each row shows the schedule name, whether it auto-runs daily and at what time, and how many zones it includes.

- **Tap RUN / STOP** on the right side of a row to start or stop that schedule
- **Tap anywhere else on a row** to open the Edit screen for that schedule
- **BACK** — return to the home screen
- **STOP ALL** — stop everything

### Edit screen
Configure a schedule's zones and timing.

- **ON / OFF toggle** next to each zone — include or exclude that zone from the schedule
- **[ − ] and [ + ]** — adjust the zone's run duration in 1-minute steps (1–99 minutes)
- **AUTO ON / OFF** — enable or disable automatic daily scheduling
- **Time controls** (visible when AUTO is on) — set the hour (1-hour steps) and minute (5-minute steps) for the daily auto-run
- **CANCEL** — go back without saving
- **SAVE** — save changes and go back
- **RUN NOW** — save changes and start the schedule immediately

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

### Schedules

#### `GET /schedules`
List all stored schedules.

```
GET http://<IP>/schedules
```
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

#### `POST /schedules`
Create a new schedule.

```
POST http://<IP>/schedules
Content-Type: application/json
```
```json
{
  "name": "Morning",
  "auto_run": true,
  "run_time": "06:00",
  "zones": [
    {"zone": 1, "duration": 300},
    {"zone": 3, "duration": 600},
    {"zone": 5, "duration": 300}
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Label for this schedule (max 31 chars) |
| `auto_run` | boolean | no | Run automatically every day at `run_time` |
| `run_time` | string | no | 24-hour time `"HH:MM"`, default `"06:00"` |
| `zones` | array | yes | Ordered list of zone steps |
| `zones[].zone` | number | yes | Zone number 1–8 |
| `zones[].duration` | number | yes | Seconds to run this zone |

Zones run one at a time in the order listed. Returns the created schedule with its assigned `id`.

#### `GET /schedules/{id}`
Get one schedule by id.

```
GET http://<IP>/schedules/1
```

#### `PUT /schedules/{id}`
Update an existing schedule. Uses the same body format as POST.

```
PUT http://<IP>/schedules/1
Content-Type: application/json
```
```json
{
  "name": "Morning Extended",
  "auto_run": true,
  "run_time": "07:00",
  "zones": [
    {"zone": 1, "duration": 600},
    {"zone": 2, "duration": 600}
  ]
}
```

#### `DELETE /schedules/{id}`
Delete a schedule. If it is currently running it will be stopped first.

```
DELETE http://<IP>/schedules/1
```
```json
{"deleted": true}
```

#### `POST /schedules/{id}/run`
Start running a schedule immediately.

```
POST http://<IP>/schedules/1/run
```
```json
{"started": true, "schedule_id": 1}
```

#### `POST /schedules/stop`
Stop whichever schedule is running and turn all zones off.

```
POST http://<IP>/schedules/stop
```
```json
{"stopped": true}
```

#### `GET /schedules/run`
Check what is currently running.

```
GET http://<IP>/schedules/run
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

The board syncs time from `pool.ntp.org` on startup and re-syncs every hour. Every 30 seconds it checks whether any schedule with `auto_run: true` has a `run_time` matching the current hour and minute. If it finds one that has not run in the last 23 hours, it starts it automatically. Only one schedule runs at a time.

Auto-run can also be configured from the Edit screen on the display without using the API.

---

## Notes

- Schedules are stored in RAM and are lost when the board is powered off. Re-create them via the API after each boot.
- The relay module is **LOW-TRIGGER**: the board pulls a pin LOW to open a valve and HIGH to close it. All relays are set to off at startup.
- The API has no authentication — anyone on your local network can control the sprinklers. Do not expose the board's port to the internet.
