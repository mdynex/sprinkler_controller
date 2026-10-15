#pragma once
#include <ArduinoJson.h>
#include "config.h"
#include "zones.h"
#include "ntp.h"

// =============================================================================
// jobs.h — Multi-job storage, JSON serialization, and runner
//
// A "job" is the single entity behind both of the things the user thinks of
// separately:
//
//   run command  — repeat == REPEAT_NONE. Fires once at runAtEpoch.
//   schedule     — repeat == REPEAT_DAILY / REPEAT_WEEKLY. Fires at
//                  runHour:runMinute on the days in daysMask.
//
// Promoting a run command to a schedule is just setting `repeat` to a
// non-NONE value on the same job — the id is stable, nothing is recreated.
// That is why there is one array here and not two.
//
// What happens after a job finishes is `onComplete`:
//   ON_COMPLETE_DELETE   — remove it (the default for one-shots)
//   ON_COMPLETE_DISABLE  — keep it listed but clear `enabled`
//   ON_COMPLETE_KEEP     — leave it armed (the default for recurring)
//
// Storage is a fixed array — no heap, no dynamic allocation. Jobs are
// persisted to KVStore by the caller (see storage.h) so they survive a power
// cycle; this file owns the in-RAM model and the runner only.
//
// The runner is non-blocking: jobTick() is called every loop() and checks
// millis() to decide when to advance a step, so WiFi and the display stay
// responsive while watering.
//
// Only ONE job runs at a time — zones are physically exclusive. A job that
// comes due while another is running is left alone and retried on a later
// tick; missed.h decides when a repeatedly-blocked job counts as missed.
// =============================================================================

#define MAX_JOBS        8
#define JOB_MAX_STEPS  64   // 8 zones x 8 cycles, generous for cycle-and-soak

// ── Data structures ────────────────────────────────────────────────────────

// One step in a job.
//
// zoneId is 1-based, EXCEPT zoneId == 0 which means "idle": hold every valve
// closed for `duration` seconds. Idle steps are what let the server's
// cycle-and-soak gaps survive the trip to the device — without them a soak
// period collapses and the next zone opens immediately.
struct ZoneStep {
  int zoneId;    // 1..ZONE_COUNT, or 0 for an idle/soak pause
  int duration;  // seconds
};

enum RepeatMode : uint8_t {
  REPEAT_NONE   = 0,   // one-shot ("run command")
  REPEAT_DAILY  = 1,   // every day at runHour:runMinute
  REPEAT_WEEKLY = 2,   // on the days in daysMask at runHour:runMinute
};

enum OnComplete : uint8_t {
  ON_COMPLETE_KEEP    = 0,
  ON_COMPLETE_DELETE  = 1,
  ON_COMPLETE_DISABLE = 2,
};

struct Job {
  bool     used;
  int      id;
  char     name[32];
  ZoneStep steps[JOB_MAX_STEPS];
  int      stepCount;
  bool     enabled;            // the list's on/off toggle
  uint8_t  repeat;             // RepeatMode
  uint8_t  daysMask;           // bit 0=Sun .. 6=Sat; used when repeat == WEEKLY
  int      runHour;            // recurring fire time
  int      runMinute;
  uint32_t runAtEpoch;         // one-shot absolute fire time (repeat == NONE)
  uint8_t  onComplete;         // OnComplete
  // Where this job came from. "server" = pushed from the app / recommendation
  // engine (treat steps as read-only on the device — the server owns the
  // multi-zone soak structure). "device" = built locally in the on-screen
  // builder. The EDIT screen uses this to decide which affordances to expose.
  char     origin[8];
  uint32_t lastRunEpoch;
  // Transient jobs exist only in RAM and are never written to flash. A rules
  // engine firing every few minutes would otherwise burn a flash write per
  // fire; and a timed zone run that dies with a power cut is exactly the
  // outcome we want anyway (the relay de-energizes and the valve closes).
  bool     isTransient;
};

// ── Global state ───────────────────────────────────────────────────────────

Job jobs[MAX_JOBS];
int nextJobId = 1;

// Tracks the one job currently running.
struct RunState {
  bool          running;
  int           jobId;      // id of the running job, 0 when idle
  int           stepIndex;
  unsigned long stepStart;
};
RunState runState = {false, 0, 0, 0};

// ── Lookup helpers ─────────────────────────────────────────────────────────

// Return a pointer to the job with this id, or nullptr.
Job* findJob(int id) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (jobs[i].used && jobs[i].id == id) return &jobs[i];
  }
  return nullptr;
}

// Claim the first free slot, or nullptr when full.
Job* allocJob() {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!jobs[i].used) return &jobs[i];
  }
  return nullptr;
}

int jobCount() {
  int n = 0;
  for (int i = 0; i < MAX_JOBS; i++) if (jobs[i].used) n++;
  return n;
}

// True if this job is the one currently running.
bool jobIsRunning(const Job& j) {
  return runState.running && runState.jobId == j.id;
}

// ── Initialisation ─────────────────────────────────────────────────────────

// Reset a job struct to sensible defaults. Does NOT assign an id or mark used.
void jobInitDefaults(Job& j) {
  j.used         = false;
  j.id           = 0;
  j.name[0]      = '\0';
  j.stepCount    = 0;
  j.enabled      = true;
  j.repeat       = REPEAT_NONE;
  j.daysMask     = 0;
  j.runHour      = 6;
  j.runMinute    = 0;
  j.runAtEpoch   = 0;
  j.onComplete   = ON_COMPLETE_DELETE;   // one-shot default; overridden below
  j.lastRunEpoch = 0;
  j.isTransient  = false;
  strncpy(j.origin, "server", sizeof(j.origin) - 1);
  j.origin[sizeof(j.origin) - 1] = '\0';
}

void jobsInit() {
  for (int i = 0; i < MAX_JOBS; i++) jobInitDefaults(jobs[i]);
  nextJobId = 1;
  runState  = {false, 0, 0, 0};
}

// ── JSON serialization ─────────────────────────────────────────────────────
//
// These write into a caller-supplied char buffer with snprintf rather than
// building an Arduino String by concatenation. With up to 8 jobs x 64 steps
// the old String approach would churn the heap badly, and this board has a
// known fragmentation sensitivity (see the [heap] log in heartbeat.h).

static const char* _repeatName(uint8_t r) {
  switch (r) {
    case REPEAT_DAILY:  return "daily";
    case REPEAT_WEEKLY: return "weekly";
    default:            return "none";
  }
}

static uint8_t _repeatFromName(const char* s, uint8_t fallback) {
  if (!s) return fallback;
  if (strcmp(s, "daily")  == 0) return REPEAT_DAILY;
  if (strcmp(s, "weekly") == 0) return REPEAT_WEEKLY;
  if (strcmp(s, "none")   == 0) return REPEAT_NONE;
  return fallback;
}

static const char* _onCompleteName(uint8_t c) {
  switch (c) {
    case ON_COMPLETE_DELETE:  return "delete";
    case ON_COMPLETE_DISABLE: return "disable";
    default:                  return "keep";
  }
}

static uint8_t _onCompleteFromName(const char* s, uint8_t fallback) {
  if (!s) return fallback;
  if (strcmp(s, "delete")  == 0) return ON_COMPLETE_DELETE;
  if (strcmp(s, "disable") == 0) return ON_COMPLETE_DISABLE;
  if (strcmp(s, "keep")    == 0) return ON_COMPLETE_KEEP;
  return fallback;
}

// Serialize one job into `buf`. Returns the number of chars written
// (excluding the null), or 0 if the buffer was too small.
//
// `auto_run` is emitted for backwards compatibility with the Android app,
// which predates the job model. It is derived, never stored.
int jobToJson(const Job& j, char* buf, size_t bufLen) {
  int n = snprintf(buf, bufLen,
    "{\"id\":%d,\"name\":\"%s\",\"origin\":\"%s\""
    ",\"enabled\":%s,\"repeat\":\"%s\",\"on_complete\":\"%s\""
    ",\"auto_run\":%s,\"run_time\":\"%02d:%02d\",\"run_at\":%lu"
    ",\"last_run\":%lu,\"running\":%s,\"transient\":%s,\"days\":[",
    j.id, j.name, j.origin,
    j.enabled ? "true" : "false",
    _repeatName(j.repeat),
    _onCompleteName(j.onComplete),
    (j.repeat != REPEAT_NONE && j.enabled) ? "true" : "false",
    j.runHour, j.runMinute,
    (unsigned long)j.runAtEpoch,
    (unsigned long)j.lastRunEpoch,
    jobIsRunning(j) ? "true" : "false",
    j.isTransient ? "true" : "false");
  if (n < 0 || (size_t)n >= bufLen) return 0;

  bool first = true;
  for (int d = 0; d < 7; d++) {
    if ((j.daysMask >> d) & 1) {
      int w = snprintf(buf + n, bufLen - n, "%s%d", first ? "" : ",", d);
      if (w < 0 || (size_t)(n + w) >= bufLen) return 0;
      n += w;
      first = false;
    }
  }

  int w = snprintf(buf + n, bufLen - n, "],\"zones\":[");
  if (w < 0 || (size_t)(n + w) >= bufLen) return 0;
  n += w;

  for (int i = 0; i < j.stepCount; i++) {
    w = snprintf(buf + n, bufLen - n, "%s{\"zone\":%d,\"duration\":%d}",
                 i ? "," : "", j.steps[i].zoneId, j.steps[i].duration);
    if (w < 0 || (size_t)(n + w) >= bufLen) return 0;
    n += w;
  }

  w = snprintf(buf + n, bufLen - n, "]}");
  if (w < 0 || (size_t)(n + w) >= bufLen) return 0;
  return n + w;
}

// Build a JSON object describing the current run state.
int runStateJson(char* buf, size_t bufLen) {
  if (!runState.running) return snprintf(buf, bufLen, "{\"running\":false}");
  Job* j = findJob(runState.jobId);
  if (!j || runState.stepIndex >= j->stepCount) {
    return snprintf(buf, bufLen, "{\"running\":false}");
  }
  const ZoneStep& step = j->steps[runState.stepIndex];
  unsigned long elapsed = (millis() - runState.stepStart) / 1000UL;
  return snprintf(buf, bufLen,
    "{\"running\":true,\"job_id\":%d,\"schedule_id\":%d,\"step\":%d"
    ",\"zone\":%d,\"idle\":%s,\"elapsed_sec\":%lu,\"duration_sec\":%d}",
    j->id, j->id, runState.stepIndex + 1,
    step.zoneId, step.zoneId == 0 ? "true" : "false",
    elapsed, step.duration);
}

// ── JSON deserialization ───────────────────────────────────────────────────

// Parse a JSON body into `j`, which the caller has pre-populated with either
// defaults (create) or the job's existing values (partial update). Only keys
// present in the body are touched, which is what makes PUT a partial update —
// and what makes "promote a run command to a schedule" a one-field change.
//
// Returns false if the JSON is unparseable, or if a create ended up with no
// valid steps.
bool parseJobBody(const String& body, Job& j, bool requireSteps) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;

  if (doc["name"].is<const char*>()) {
    strncpy(j.name, doc["name"].as<const char*>(), sizeof(j.name) - 1);
    j.name[sizeof(j.name) - 1] = '\0';
  }

  if (doc["origin"].is<const char*>()) {
    strncpy(j.origin, doc["origin"].as<const char*>(), sizeof(j.origin) - 1);
    j.origin[sizeof(j.origin) - 1] = '\0';
  }

  if (doc["enabled"].is<bool>()) j.enabled = doc["enabled"].as<bool>();

  // Days: an explicit array wins and implies a weekly repeat. Otherwise fall
  // back to the legacy auto_run flag meaning "every day", so the app's older
  // send-schedule payloads keep working unchanged.
  bool daysGiven = false;
  if (doc["days"].is<JsonArray>()) {
    j.daysMask = 0;
    for (JsonVariant v : doc["days"].as<JsonArray>()) {
      int d = v.as<int>();
      if (d >= 0 && d <= 6) j.daysMask |= (uint8_t)(1 << d);
    }
    daysGiven = true;
  } else if (doc["auto_run"].is<bool>() && doc["auto_run"].as<bool>()) {
    j.daysMask = 0x7F;
    daysGiven  = true;
  }

  // Explicit repeat wins; otherwise infer it from the days that arrived.
  if (doc["repeat"].is<const char*>()) {
    j.repeat = _repeatFromName(doc["repeat"].as<const char*>(), j.repeat);
  } else if (daysGiven) {
    j.repeat = (j.daysMask == 0) ? REPEAT_NONE
             : (j.daysMask == 0x7F ? REPEAT_DAILY : REPEAT_WEEKLY);
  }
  // A daily job fires every day regardless of what the mask happened to hold.
  if (j.repeat == REPEAT_DAILY) j.daysMask = 0x7F;

  if (doc["on_complete"].is<const char*>()) {
    j.onComplete = _onCompleteFromName(doc["on_complete"].as<const char*>(), j.onComplete);
  } else if (doc["repeat"].is<const char*>() || daysGiven) {
    // Repeat changed but the caller didn't say what to do on completion —
    // pick the sane default for the new shape. A promoted run command should
    // stop deleting itself, or it would vanish after its first run.
    j.onComplete = (j.repeat == REPEAT_NONE) ? ON_COMPLETE_DELETE : ON_COMPLETE_KEEP;
  }

  if (doc["run_time"].is<const char*>()) {
    int h = 0, m = 0;
    if (sscanf(doc["run_time"].as<const char*>(), "%d:%d", &h, &m) == 2 &&
        h >= 0 && h < 24 && m >= 0 && m < 60) {
      j.runHour   = h;
      j.runMinute = m;
    }
  }

  if (!doc["run_at"].isNull()) {
    j.runAtEpoch = (uint32_t)doc["run_at"].as<unsigned long>();
  }

  if (doc["zones"].is<JsonArray>()) {
    j.stepCount = 0;
    for (JsonObject z : doc["zones"].as<JsonArray>()) {
      if (j.stepCount >= JOB_MAX_STEPS) break;
      int zoneId   = z["zone"].as<int>();
      int duration = z["duration"].as<int>();
      // zoneId 0 is a legal idle/soak step; 1..ZONE_COUNT are real valves.
      if (zoneId < 0 || zoneId > ZONE_COUNT || duration <= 0) continue;
      j.steps[j.stepCount].zoneId   = zoneId;
      j.steps[j.stepCount].duration = duration;
      j.stepCount++;
    }
  }

  return requireSteps ? (j.stepCount > 0) : true;
}

// ── Runner ─────────────────────────────────────────────────────────────────

// Index of the first step whose zone is actually runnable, starting at `from`.
// Idle steps (zone 0) always count; real zones must be enabled. Returns -1 if
// there is nothing left to run.
static int _nextRunnableStep(const Job& j, int from) {
  for (int i = from; i < j.stepCount; i++) {
    if (j.steps[i].zoneId == 0) return i;                    // idle pause
    if (zoneEnabled[j.steps[i].zoneId - 1]) return i;        // enabled valve
  }
  return -1;
}

// Open the valve for a step (or close everything, for an idle step).
static void _applyStep(const ZoneStep& step) {
  if (step.zoneId == 0) allZonesOff();
  else                  setZone(step.zoneId - 1, true);
}

void stopJob() {
  allZonesOff();
  runState.running   = false;
  runState.jobId     = 0;
  runState.stepIndex = 0;
}

// Start a job immediately. Stops whatever was running first — zones are
// exclusive, so there is no meaningful "run both".
// Returns false if the job has no runnable steps (e.g. every zone disabled).
bool startJob(Job& j) {
  allZonesOff();
  runState.running = false;

  int first = _nextRunnableStep(j, 0);
  if (first < 0) return false;

  runState.running   = true;
  runState.jobId     = j.id;
  runState.stepIndex = first;
  runState.stepStart = millis();
  _applyStep(j.steps[first]);
  return true;
}

// Apply the job's completion policy. Declared here and called by jobTick();
// the storage write is injected by the .ino so this header stays free of a
// storage.h dependency (storage.h includes nothing from here).
void (*onJobPersistNeeded)() = nullptr;

static void _finishJob(Job& j) {
  j.lastRunEpoch = ntpReady ? ntpEpoch() : 0;
  bool durable   = !j.isTransient;

  switch (j.onComplete) {
    case ON_COMPLETE_DELETE:
      Serial.print("[job] "); Serial.print(j.name); Serial.println(" done - deleting");
      jobInitDefaults(j);      // clears `used`, freeing the slot
      break;
    case ON_COMPLETE_DISABLE:
      Serial.print("[job] "); Serial.print(j.name); Serial.println(" done - disabling");
      j.enabled = false;
      break;
    default:
      Serial.print("[job] "); Serial.print(j.name); Serial.println(" done");
      break;
  }

  // A one-shot that survives its run (on_complete=keep) must not fire again:
  // its runAtEpoch is now in the past, so the scheduler would restart it on
  // every check. Clearing the time disarms it while leaving it in the list.
  if (j.used && j.repeat == REPEAT_NONE) j.runAtEpoch = 0;

  if (durable && onJobPersistNeeded) onJobPersistNeeded();
}

// Called every loop(). Advances the running job's step when its duration is up.
void jobTick() {
  if (!runState.running) return;

  Job* j = findJob(runState.jobId);
  if (!j) { stopJob(); return; }   // job deleted mid-run

  if (runState.stepIndex >= j->stepCount) { stopJob(); return; }

  const ZoneStep& step = j->steps[runState.stepIndex];
  if (millis() - runState.stepStart < (unsigned long)step.duration * 1000UL) return;

  if (step.zoneId != 0) setZone(step.zoneId - 1, false);

  int next = _nextRunnableStep(*j, runState.stepIndex + 1);
  if (next < 0) {
    // All steps done. Clear runState before applying the completion policy —
    // _finishJob may wipe the struct, and nothing may observe a runState that
    // still points at a freed slot.
    stopJob();
    _finishJob(*j);
    return;
  }

  runState.stepIndex = next;
  runState.stepStart = millis();
  _applyStep(j->steps[next]);
}

// True if this job's recurring fire time matches the given wall clock.
static bool _recurringDueAt(const Job& j, int hour, int minute, int dow) {
  if (j.repeat == REPEAT_NONE)      return false;
  if (j.runHour != hour)            return false;
  if (j.runMinute != minute)        return false;
  return ((j.daysMask >> dow) & 1) != 0;
}

// Called every loop(). Checks every 30 s whether any job is due.
//
// Only one job can run at a time; if something is already running, a job that
// comes due is simply left for the next check. Because the check runs every
// 30 s and the due-window is a whole minute, it will be picked up as soon as
// the zones free up — and if it stays blocked long enough, the boot-time
// missed-job scan in missed.h will report it.
void jobSchedulerCheck() {
  if (!ntpReady)        return;
  if (runState.running) return;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 30000UL) return;
  lastCheck = millis();

  int           nowHour   = ntpHour();
  int           nowMinute = ntpMinute();
  int           dow       = ntpDayOfWeek();
  unsigned long nowEpoch  = ntpEpoch();

  for (int i = 0; i < MAX_JOBS; i++) {
    Job& j = jobs[i];
    if (!j.used || !j.enabled) continue;

    bool due = false;
    if (j.repeat == REPEAT_NONE) {
      due = (j.runAtEpoch > 0 && nowEpoch >= j.runAtEpoch);
    } else {
      // The 23-hour guard stops a job re-firing within the same minute window.
      due = _recurringDueAt(j, nowHour, nowMinute, dow) &&
            (nowEpoch - j.lastRunEpoch >= 23 * 3600UL);
    }
    if (!due) continue;

    Serial.print("[job] Auto-starting: "); Serial.println(j.name);
    if (startJob(j)) {
      j.lastRunEpoch = nowEpoch;
      return;   // one at a time
    }
    // Nothing runnable (all zones disabled) — treat it as having run so a
    // one-shot still retires instead of retrying forever.
    j.lastRunEpoch = nowEpoch;
    _finishJob(j);
    return;
  }
}
