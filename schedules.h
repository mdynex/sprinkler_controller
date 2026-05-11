#pragma once
#include <ArduinoJson.h>
#include "config.h"
#include "zones.h"
#include "ntp.h"


// ── Data structures ────────────────────────────────────────────────────────

struct ZoneStep {
  int zoneId;    // 1-based zone number
  int duration;  // seconds to run
};

struct Schedule {
  bool     used;
  int      id;
  char     name[32];
  ZoneStep steps[MAX_ZONE_STEPS];
  int      stepCount;
  // Auto-run settings
  bool     autoRun;   // true = run daily at the time below
  int      runHour;   // 0–23
  int      runMinute; // 0–59
};

// ── State ──────────────────────────────────────────────────────────────────

Schedule      schedules[MAX_SCHEDULES];
int           nextScheduleId = 1;
unsigned long lastRunEpoch[MAX_SCHEDULES] = {0};  // epoch sec of last auto-run

struct RunState {
  bool          running;
  int           scheduleIndex;
  int           stepIndex;
  unsigned long stepStart;
};
RunState runState = {false, -1, 0, 0};

// ── Init ───────────────────────────────────────────────────────────────────

void schedulesInit() {
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    schedules[i].used      = false;
    schedules[i].autoRun   = false;
    schedules[i].runHour   = 6;
    schedules[i].runMinute = 0;
  }
}

// ── Lookup helpers ─────────────────────────────────────────────────────────

int findScheduleIndex(int id) {
  for (int i = 0; i < MAX_SCHEDULES; i++)
    if (schedules[i].used && schedules[i].id == id) return i;
  return -1;
}

int freeSlot() {
  for (int i = 0; i < MAX_SCHEDULES; i++)
    if (!schedules[i].used) return i;
  return -1;
}

// ── JSON serialization ─────────────────────────────────────────────────────

String scheduleToJson(const Schedule& s) {
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", s.runHour, s.runMinute);

  String json = "{\"id\":"        + String(s.id) +
                ",\"name\":\""    + s.name + "\"" +
                ",\"auto_run\":"  + (s.autoRun ? "true" : "false") +
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

bool parseScheduleBody(const String& body, Schedule& s) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;

  if (doc["name"].is<const char*>())
    strncpy(s.name, doc["name"].as<const char*>(), sizeof(s.name) - 1);

  if (doc["auto_run"].is<bool>())
    s.autoRun = doc["auto_run"].as<bool>();

  // run_time as "HH:MM"
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
    if (s.stepCount >= MAX_ZONE_STEPS) break;
    int zoneId   = z["zone"].as<int>();
    int duration = z["duration"].as<int>();
    if (zoneId < 1 || zoneId > ZONE_COUNT || duration <= 0) continue;
    s.steps[s.stepCount++] = {zoneId, duration};
  }
  return s.stepCount > 0;
}

// ── Schedule runner (non-blocking) ─────────────────────────────────────────

void startSchedule(int index) {
  allZonesOff();
  runState.running       = true;
  runState.scheduleIndex = index;
  runState.stepIndex     = 0;
  runState.stepStart     = millis();
  setZone(schedules[index].steps[0].zoneId - 1, true);
}

void stopSchedule() {
  allZonesOff();
  runState.running = false;
}

// Advances the active schedule one step when its zone's time is up.
void scheduleTick() {
  if (!runState.running) return;

  Schedule& sched = schedules[runState.scheduleIndex];
  ZoneStep& step  = sched.steps[runState.stepIndex];

  if (millis() - runState.stepStart >= (unsigned long)step.duration * 1000UL) {
    setZone(step.zoneId - 1, false);
    runState.stepIndex++;

    if (runState.stepIndex >= sched.stepCount) {
      runState.running = false;
    } else {
      ZoneStep& next = sched.steps[runState.stepIndex];
      setZone(next.zoneId - 1, true);
      runState.stepStart = millis();
    }
  }
}

// Checks every 30 seconds whether any auto-run schedule is due.
// Fires at most once per 23 hours per schedule to prevent double-firing.
void schedulerCheck() {
  if (!ntpReady) return;
  if (runState.running) return;  // don't interrupt an active run

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
    if (nowEpoch - lastRunEpoch[i] < 23 * 3600UL) continue;  // already ran today

    Serial.print("Auto-starting schedule: ");
    Serial.println(schedules[i].name);
    startSchedule(i);
    lastRunEpoch[i] = nowEpoch;
    break;  // only one schedule at a time
  }
}
