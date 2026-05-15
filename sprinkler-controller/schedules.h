#pragma once
#include <ArduinoJson.h>
#include "config.h"
#include "zones.h"
#include "ntp.h"

// =============================================================================
// schedules.h — Single-schedule storage, JSON serialization, and runner
//
// Only one schedule lives in memory at a time. Sending a new schedule via
// POST /schedules replaces whatever was stored before.
//
// The runner is non-blocking: scheduleTick() is called every loop() and
// simply checks millis() to see if it's time to advance to the next step.
// This means the WiFi server and display remain responsive while watering.
//
// Schedules are stored in RAM only — they are lost when the board powers off.
//
// Auto-run: the schedule can be given a daily run time (hour + minute).
// schedulerCheck() runs every 30 seconds and fires the schedule if its time
// matches the current local time and it hasn't run in the last 23 hours.
// =============================================================================

// Maximum zone steps per schedule. Internal implementation detail only —
// not a user-facing config. 128 covers 8 zones × 16 soak cycles with room
// to spare.
#define SCHEDULE_MAX_STEPS 128

// ── Data structures ────────────────────────────────────────────────────────

// One step in a schedule: a zone number and how long to run it.
struct ZoneStep {
  int zoneId;    // 1-based zone number
  int duration;  // seconds to keep this zone open
};

// The single schedule stored in memory.
struct Schedule {
  bool     used;                         // false = no schedule loaded
  int      id;                           // ID assigned at creation, increments on replace
  char     name[32];                     // human-readable name
  ZoneStep steps[SCHEDULE_MAX_STEPS];    // ordered list of zone steps
  int      stepCount;                    // number of active steps
  bool     autoRun;                      // true = fire automatically every day
  int      runHour;                      // 0–23, local time
  int      runMinute;                    // 0–59, local time
};

// ── Global state ───────────────────────────────────────────────────────────

Schedule      theSchedule   = {false};
int           nextScheduleId = 1;
unsigned long lastRunEpoch   = 0;

// Tracks which step is currently running.
struct RunState {
  bool          running;
  int           stepIndex;
  unsigned long stepStart;
};
RunState runState = {false, 0, 0};

// ── Initialisation ─────────────────────────────────────────────────────────

void schedulesInit() {
  theSchedule.used      = false;
  theSchedule.autoRun   = false;
  theSchedule.runHour   = 6;
  theSchedule.runMinute = 0;
  lastRunEpoch          = 0;
}

// ── JSON serialization ─────────────────────────────────────────────────────

// Convert the schedule to a JSON object string.
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

// Returns an array with the one schedule, or an empty array.
String schedulesListJson() {
  if (!theSchedule.used) return "[]";
  return "[" + scheduleToJson(theSchedule) + "]";
}

// Build a JSON object describing the current run state.
String runStateJson() {
  if (!runState.running) return "{\"running\":false}";
  ZoneStep& step    = theSchedule.steps[runState.stepIndex];
  unsigned long elapsed = (millis() - runState.stepStart) / 1000UL;
  return "{\"running\":true"
         ",\"schedule_id\":"  + String(theSchedule.id) +
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

  s.autoRun = doc["auto_run"].is<bool>() ? doc["auto_run"].as<bool>() : false;

  if (doc["run_time"].is<const char*>()) {
    const char* t = doc["run_time"].as<const char*>();
    int h = 0, m = 0;
    if (sscanf(t, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
      s.runHour   = h;
      s.runMinute = m;
    }
  }

  JsonArray zones = doc["zones"].as<JsonArray>();
  s.stepCount = 0;
  for (JsonObject z : zones) {
    if (s.stepCount >= SCHEDULE_MAX_STEPS) break;
    int zoneId   = z["zone"].as<int>();
    int duration = z["duration"].as<int>();
    if (zoneId < 1 || zoneId > ZONE_COUNT || duration <= 0) continue;
    s.steps[s.stepCount++] = {zoneId, duration};
  }
  return s.stepCount > 0;
}

// ── Schedule runner ────────────────────────────────────────────────────────

// Start running the schedule, skipping any steps whose zone is disabled.
// Stops any currently running schedule first.
void startSchedule() {
  if (!theSchedule.used) return;
  allZonesOff();
  runState.running = false;

  // Find the first step with an enabled zone
  int first = -1;
  for (int i = 0; i < theSchedule.stepCount; i++) {
    if (zoneEnabled[theSchedule.steps[i].zoneId - 1]) { first = i; break; }
  }
  if (first < 0) return;  // nothing enabled to run

  runState.running   = true;
  runState.stepIndex = first;
  runState.stepStart = millis();
  setZone(theSchedule.steps[first].zoneId - 1, true);
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

  ZoneStep& step = theSchedule.steps[runState.stepIndex];

  if (millis() - runState.stepStart >= (unsigned long)step.duration * 1000UL) {
    setZone(step.zoneId - 1, false);

    // Advance to the next step that has an enabled zone
    int next = runState.stepIndex + 1;
    while (next < theSchedule.stepCount && !zoneEnabled[theSchedule.steps[next].zoneId - 1]) next++;

    if (next >= theSchedule.stepCount) {
      runState.running = false;  // all done
    } else {
      runState.stepIndex = next;
      setZone(theSchedule.steps[next].zoneId - 1, true);
      runState.stepStart = millis();
    }
  }
}

// Called every loop(). Checks every 30 seconds whether the auto-run schedule
// is due. Fires if:
//   - a schedule is loaded and auto_run is enabled
//   - the current hour and minute match its run_time
//   - it has not already run within the last 23 hours
// Does nothing if a schedule is already running.
void schedulerCheck() {
  if (!ntpReady)          return;
  if (runState.running)   return;
  if (!theSchedule.used)  return;
  if (!theSchedule.autoRun) return;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 30000UL) return;
  lastCheck = millis();

  int           nowHour   = ntpHour();
  int           nowMinute = ntpMinute();
  unsigned long nowEpoch  = ntpEpoch();

  if (theSchedule.runHour   != nowHour)   return;
  if (theSchedule.runMinute != nowMinute) return;
  if (nowEpoch - lastRunEpoch < 23 * 3600UL) return;

  Serial.print("Auto-starting schedule: ");
  Serial.println(theSchedule.name);
  startSchedule();
  lastRunEpoch = nowEpoch;
}
