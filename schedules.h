#pragma once
#include <ArduinoJson.h>
#include "config.h"
#include "zones.h"
#include "ntp.h"

// =============================================================================
// schedules.h — Schedule storage, JSON serialization, and runner
//
// A Schedule is an ordered list of zone steps. Each step specifies which
// zone to water and for how long. When a schedule runs, zones activate one
// at a time in step order — the next zone doesn't start until the current
// one's duration has elapsed.
//
// The runner is non-blocking: scheduleTick() is called every loop() and
// simply checks millis() to see if it's time to advance to the next step.
// This means the WiFi server and display remain responsive while watering.
//
// Schedules are stored in RAM only. They are lost when the board powers off.
// Use the API to recreate them, or add EEPROM/SD persistence later.
//
// Auto-run: each schedule can be given a daily run time (hour + minute).
// schedulerCheck() runs every 30 seconds and fires any schedule whose time
// matches the current local time, as long as it hasn't run in the last 23h.
// =============================================================================

// ── Data structures ────────────────────────────────────────────────────────

// One step in a schedule: a zone number and how long to run it.
struct ZoneStep {
  int zoneId;    // 1-based zone number (matches the number shown on the display)
  int duration;  // seconds to keep this zone open
};

// A complete schedule stored in memory.
struct Schedule {
  bool     used;                    // false = this slot is empty
  int      id;                      // unique ID assigned at creation
  char     name[32];                // human-readable name (set via API)
  ZoneStep steps[MAX_ZONE_STEPS];   // ordered list of zone steps
  int      stepCount;               // number of active steps
  bool     autoRun;                 // true = fire automatically every day
  int      runHour;                 // 0–23, local time
  int      runMinute;               // 0–59, local time
};

// ── Global state ───────────────────────────────────────────────────────────

Schedule      schedules[MAX_SCHEDULES];
int           nextScheduleId = 1;                         // increments with each new schedule
unsigned long lastRunEpoch[MAX_SCHEDULES] = {0};          // epoch time of last auto-run per slot

// Tracks which schedule is currently running and which step is active.
struct RunState {
  bool          running;        // true while a schedule is in progress
  int           scheduleIndex;  // index into schedules[] (not the schedule ID)
  int           stepIndex;      // which step is currently active
  unsigned long stepStart;      // millis() when the current step began
};
RunState runState = {false, -1, 0, 0};

// ── Initialisation ─────────────────────────────────────────────────────────

void schedulesInit() {
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    schedules[i].used      = false;
    schedules[i].autoRun   = false;
    schedules[i].runHour   = 6;    // default auto-run time: 6:00 AM
    schedules[i].runMinute = 0;
  }
}

// ── Lookup helpers ─────────────────────────────────────────────────────────

// Find the array index for a schedule by its public ID. Returns -1 if not found.
int findScheduleIndex(int id) {
  for (int i = 0; i < MAX_SCHEDULES; i++)
    if (schedules[i].used && schedules[i].id == id) return i;
  return -1;
}

// Find the first empty slot in schedules[]. Returns -1 if all slots are full.
int freeSlot() {
  for (int i = 0; i < MAX_SCHEDULES; i++)
    if (!schedules[i].used) return i;
  return -1;
}

// ── JSON serialization ─────────────────────────────────────────────────────

// Convert one schedule to a JSON object string.
String scheduleToJson(const Schedule& s) {
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", s.runHour, s.runMinute);

  String json = "{\"id\":"         + String(s.id) +
                ",\"name\":\""     + s.name + "\"" +
                ",\"auto_run\":"   + (s.autoRun ? "true" : "false") +
                ",\"run_time\":\"" + timeBuf + "\"" +
                ",\"zones\":[";
  for (int i = 0; i < s.stepCount; i++) {
    if (i > 0) json += ",";
    json += "{\"zone\":"     + String(s.steps[i].zoneId) +
            ",\"duration\":" + String(s.steps[i].duration) + "}";
  }
  json += "]}";
  return json;
}

// Convert all stored schedules to a JSON array string.
String schedulesListJson() {
  String json = "[";
  bool first = true;
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (!schedules[i].used) continue;
    if (!first) json += ",";
    json += scheduleToJson(schedules[i]);
    first = false;
  }
  json += "]";
  return json;
}

// Build a JSON object describing the current run state.
// Used by GET /schedules/run.
String runStateJson() {
  if (!runState.running) return "{\"running\":false}";
  Schedule& s    = schedules[runState.scheduleIndex];
  ZoneStep& step = s.steps[runState.stepIndex];
  unsigned long elapsed = (millis() - runState.stepStart) / 1000UL;
  return "{\"running\":true"
         ",\"schedule_id\":"  + String(s.id) +
         ",\"step\":"         + String(runState.stepIndex + 1) +
         ",\"zone\":"         + String(step.zoneId) +
         ",\"elapsed_sec\":"  + String(elapsed) +
         ",\"duration_sec\":" + String(step.duration) + "}";
}

// ── JSON deserialization ───────────────────────────────────────────────────

// Parse a JSON request body into a Schedule struct.
// Fills name, autoRun, runHour, runMinute, and steps.
// Does NOT set id or used — the caller handles those.
// Returns false if the JSON is invalid or contains no valid zone steps.
bool parseScheduleBody(const String& body, Schedule& s) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;

  if (doc["name"].is<const char*>())
    strncpy(s.name, doc["name"].as<const char*>(), sizeof(s.name) - 1);

  if (doc["auto_run"].is<bool>())
    s.autoRun = doc["auto_run"].as<bool>();

  // run_time must be a string in "HH:MM" 24-hour format
  if (doc["run_time"].is<const char*>()) {
    const char* t = doc["run_time"].as<const char*>();
    int h = 0, m = 0;
    if (sscanf(t, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
      s.runHour   = h;
      s.runMinute = m;
    }
  }

  // zones is an array of {"zone": N, "duration": S} objects
  JsonArray zones = doc["zones"].as<JsonArray>();
  s.stepCount = 0;
  for (JsonObject z : zones) {
    if (s.stepCount >= MAX_ZONE_STEPS) break;
    int zoneId   = z["zone"].as<int>();
    int duration = z["duration"].as<int>();
    if (zoneId < 1 || zoneId > ZONE_COUNT || duration <= 0) continue;
    s.steps[s.stepCount++] = {zoneId, duration};
  }
  return s.stepCount > 0;
}

// ── Schedule runner ────────────────────────────────────────────────────────

// Start running a schedule from its first step.
// Stops any currently running schedule first.
void startSchedule(int index) {
  allZonesOff();
  runState.running       = true;
  runState.scheduleIndex = index;
  runState.stepIndex     = 0;
  runState.stepStart     = millis();
  setZone(schedules[index].steps[0].zoneId - 1, true);
}

// Stop the running schedule and turn all zones off immediately.
void stopSchedule() {
  allZonesOff();
  runState.running = false;
}

// Called every loop(). Checks if the current zone step has finished and, if
// so, turns it off and starts the next one. When all steps are done, the
// schedule completes and runState.running is set to false.
void scheduleTick() {
  if (!runState.running) return;

  Schedule& sched = schedules[runState.scheduleIndex];
  ZoneStep& step  = sched.steps[runState.stepIndex];

  if (millis() - runState.stepStart >= (unsigned long)step.duration * 1000UL) {
    setZone(step.zoneId - 1, false);
    runState.stepIndex++;

    if (runState.stepIndex >= sched.stepCount) {
      runState.running = false;  // all steps done
    } else {
      ZoneStep& next = sched.steps[runState.stepIndex];
      setZone(next.zoneId - 1, true);
      runState.stepStart = millis();
    }
  }
}

// Called every loop(). Checks every 30 seconds whether any auto-run schedule
// is due. A schedule fires if:
//   - auto_run is enabled
//   - the current hour and minute match its run_time
//   - it has not already run within the last 23 hours
// Only one schedule can start per check. If a schedule is already running,
// this function does nothing until it finishes.
void schedulerCheck() {
  if (!ntpReady) return;
  if (runState.running) return;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 30000UL) return;
  lastCheck = millis();

  int nowHour   = ntpHour();
  int nowMinute = ntpMinute();
  unsigned long nowEpoch = ntpEpoch();

  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (!schedules[i].used)    continue;
    if (!schedules[i].autoRun) continue;
    if (schedules[i].runHour   != nowHour)   continue;
    if (schedules[i].runMinute != nowMinute) continue;
    if (nowEpoch - lastRunEpoch[i] < 23 * 3600UL) continue;

    Serial.print("Auto-starting schedule: ");
    Serial.println(schedules[i].name);
    startSchedule(i);
    lastRunEpoch[i] = nowEpoch;
    break;
  }
}
