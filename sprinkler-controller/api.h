#pragma once
#include "http_utils.h"
#include "zones.h"
#include "jobs.h"
#include "storage.h"
#include "missed.h"
#include "ntp.h"
#include "state.h"

extern bool needsRedraw;   // defined in display.h — set true to trigger full screen refresh
extern void wakeDisplay(); // defined in display.h — wakes backlight and resets sleep timer
void routeProvision(WiFiClient& client, HttpRequest& req);  // defined in pairing.h

// =============================================================================
// api.h — REST API route handlers
//
// All incoming HTTP requests are dispatched here by handleApi(). Each route is
// matched by HTTP method + URL path. Responses are always JSON.
//
//   GET  /time                   → current synced time and epoch
//   GET  /zones                  → list all zones and their on/off state
//   POST /zones/{id}/on|off      → turn zone {id} on/off  (id = 1–8)
//   PUT  /zones/{id}             → update zone settings (enabled, rate)
//
//   GET    /jobs                 → list every job
//   POST   /jobs                 → ADD a job (does not replace existing ones)
//   GET    /jobs/{id}            → one job
//   PUT    /jobs/{id}            → partial update; this is how a run command
//                                  is promoted to a schedule (set "repeat")
//   DELETE /jobs/{id}            → remove a job
//   POST   /jobs/{id}/run        → run a job immediately
//   POST   /jobs/stop            → stop whatever is running
//   GET    /jobs/run             → current run status
//   GET    /jobs/missed          → jobs missed while the device was off
//
//   POST /command                → generic device command (rules engine)
//
// The older /schedules* paths are kept as aliases onto the same handlers so
// the Android app keeps working untouched.
//
// Job JSON body (POST and PUT):
//   {
//     "name":        "Morning",      required on create
//     "repeat":      "none",         "none" | "daily" | "weekly"
//     "days":        [1,3,5],        0=Sun..6=Sat, implies weekly
//     "run_time":    "06:00",        recurring fire time, 24h HH:MM
//     "run_at":      1723800000,     one-shot absolute epoch
//     "on_complete": "delete",       "keep" | "delete" | "disable"
//     "enabled":     true,
//     "zones": [                     required on create
//       { "zone": 1, "duration": 300 },   zone 1-8, seconds
//       { "zone": 0, "duration": 600 }    zone 0 = idle/soak pause
//     ]
//   }
// =============================================================================

// Scratch buffer for serializing one job. Static so a large response never
// touches the heap — this board has a known fragmentation sensitivity.
static char _jobBuf[2048];

// ── Helpers ────────────────────────────────────────────────────────────────

static void _sendJob(WiFiClient& client, int status, const Job& j) {
  int n = jobToJson(j, _jobBuf, sizeof(_jobBuf));
  if (n <= 0) { sendResponse(client, 500, "{\"error\":\"job too large to encode\"}"); return; }
  sendResponseRaw(client, status, _jobBuf, n);
}

static void _persistAndRefresh() {
  storage::saveAllJobs();
  needsRedraw = true;
}

// Extract the numeric id from a path like "/jobs/12" or "/jobs/12/run".
static int _idFromPath(const String& path, int prefixLen) {
  return path.substring(prefixLen).toInt();
}

// Both /jobs and /schedules map onto the same handlers; this reports how many
// characters to skip to reach the id.
static int _prefixLen(const String& p) {
  return p.startsWith("/jobs/") ? 6 : 11;   // "/jobs/" vs "/schedules/"
}

// ── Job list (streamed) ────────────────────────────────────────────────────

// Serialize the whole job array without ever holding it in one allocation.
// Pass 1 measures, pass 2 writes — so the response still carries an exact
// Content-Length for clients that need one.
static void _sendJobList(WiFiClient& client) {
  int total = 2;   // "[" and "]"
  int used  = 0;
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!jobs[i].used) continue;
    int n = jobToJson(jobs[i], _jobBuf, sizeof(_jobBuf));
    if (n <= 0) continue;
    if (used > 0) total += 1;   // comma
    total += n;
    used++;
  }

  sendHeaders(client, 200, total);
  client.print("[");
  bool first = true;
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!jobs[i].used) continue;
    int n = jobToJson(jobs[i], _jobBuf, sizeof(_jobBuf));
    if (n <= 0) continue;
    if (!first) client.print(",");
    client.write((const uint8_t*)_jobBuf, n);
    first = false;
  }
  client.print("]");
}

// ── Zones ──────────────────────────────────────────────────────────────────

// Handle POST /zones/{id}/on|off
void routeZoneControl(WiFiClient& client, HttpRequest& req) {
  String rest  = req.path.substring(7);  // strip leading "/zones/"
  int    slash = rest.indexOf('/');
  if (slash < 0) { sendResponse(client, 400, "{\"error\":\"missing action\"}"); return; }

  int    id     = rest.substring(0, slash).toInt() - 1;  // convert to 0-based index
  String action = rest.substring(slash + 1);

  if (id < 0 || id >= ZONE_COUNT) {
    sendResponse(client, 404, "{\"error\":\"zone not found\"}"); return;
  }
  if (action != "on" && action != "off") {
    sendResponse(client, 400, "{\"error\":\"action must be on or off\"}"); return;
  }
  setZone(id, action == "on");
  needsRedraw = true;
  wakeDisplay();
  sendResponse(client, 200,
    "{\"id\":" + String(id + 1) + ",\"state\":\"" + action + "\"}");
}

// ── Generic command ────────────────────────────────────────────────────────

// POST /command — the device-agnostic entry point the server's rules engine
// and time-based actions post to (see _send_command in routes/rules/engine.py).
//
// Rather than duplicating the runner, a command with a duration becomes a
// one-shot job that deletes itself: exactly the same machinery a "water once
// tomorrow" push from the server uses.
void routeCommand(WiFiClient& client, HttpRequest& req) {
  JsonDocument doc;
  if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
    sendResponse(client, 400, "{\"error\":\"invalid JSON body\"}"); return;
  }

  const char* action = doc["action"].is<const char*>() ? doc["action"].as<const char*>() : "";

  if (strcmp(action, "stop") == 0) {
    stopJob();
    needsRedraw = true;
    wakeDisplay();
    sendResponse(client, 200, "{\"ok\":true,\"action\":\"stop\"}");
    return;
  }

  if (strcmp(action, "run_job") == 0) {
    int jobId = doc["job_id"].as<int>();
    Job* j = findJob(jobId);
    if (!j) { sendResponse(client, 404, "{\"error\":\"job not found\"}"); return; }
    if (!startJob(*j)) {
      sendResponse(client, 409, "{\"error\":\"job has no runnable zones\"}"); return;
    }
    needsRedraw = true;
    wakeDisplay();
    sendResponse(client, 200, "{\"ok\":true,\"action\":\"run_job\"}");
    return;
  }

  if (strcmp(action, "zone_on") == 0 || strcmp(action, "zone_off") == 0) {
    int zoneId = doc["zone_id"].as<int>();
    if (zoneId < 1 || zoneId > ZONE_COUNT) {
      sendResponse(client, 400, "{\"error\":\"zone_id out of range\"}"); return;
    }

    if (strcmp(action, "zone_off") == 0) {
      setZone(zoneId - 1, false);
      needsRedraw = true;
      wakeDisplay();
      sendResponse(client, 200, "{\"ok\":true,\"action\":\"zone_off\"}");
      return;
    }

    int duration = doc["duration"].as<int>();
    if (duration <= 0) {
      // No duration — a plain latch. Stays on until something turns it off.
      setZone(zoneId - 1, true);
      needsRedraw = true;
      wakeDisplay();
      sendResponse(client, 200, "{\"ok\":true,\"action\":\"zone_on\"}");
      return;
    }

    // Timed run — becomes a self-deleting one-shot so the runner closes the
    // valve for us even if the server never calls back. Marked transient: a
    // rule firing on an interval must not burn a flash write every time.
    Job* j = allocJob();
    if (!j) { sendResponse(client, 507, "{\"error\":\"job storage full\"}"); return; }
    jobInitDefaults(*j);
    j->used        = true;
    j->id          = nextJobId++;
    j->enabled     = true;
    j->repeat      = REPEAT_NONE;
    j->onComplete  = ON_COMPLETE_DELETE;
    j->isTransient = true;
    j->stepCount   = 1;
    j->steps[0]    = { zoneId, duration };
    j->runAtEpoch  = ntpReady ? (uint32_t)ntpEpoch() : 0;
    snprintf(j->name, sizeof(j->name), "Zone %d (%ds)", zoneId, duration);
    strncpy(j->origin, "server", sizeof(j->origin) - 1);

    if (!startJob(*j)) {
      jobInitDefaults(*j);
      sendResponse(client, 409, "{\"error\":\"zone is disabled\"}"); return;
    }
    needsRedraw = true;
    wakeDisplay();
    _sendJob(client, 200, *j);
    return;
  }

  // Unknown action — echo it back so a misconfigured rule is diagnosable from
  // the server log instead of failing as a bare 404.
  snprintf(_jobBuf, sizeof(_jobBuf),
           "{\"error\":\"unknown action\",\"action\":\"%.64s\"}", action);
  sendResponse(client, 400, String(_jobBuf));
}

// ── Dispatcher ─────────────────────────────────────────────────────────────

void handleApi(WiFiClient& client, HttpRequest& req) {
  String& m = req.method;
  String& p = req.path;

  // POST /provision is the only route accepted while the device is in
  // STATE_PAIRING. Once the device is RUNNING it returns 409.
  if (m == "POST" && p == "/provision") {
    routeProvision(client, req);
    return;
  }

  // Block all other routes until the device is paired with a server.
  if (deviceState != STATE_RUNNING) {
    sendResponse(client, 503, "{\"error\":\"device unpaired\"}");
    return;
  }

  // Treat /schedules* as an alias for /jobs* throughout.
  bool isJobPath = p.startsWith("/jobs") || p.startsWith("/schedules");
  String tail;   // the part after the collection name, e.g. "", "/run", "/12"
  if (isJobPath) {
    tail = p.startsWith("/jobs") ? p.substring(5) : p.substring(10);
  }

  // GET /time
  if (m == "GET" && p == "/time") {
    sendResponse(client, 200, currentTimeJson());

  // GET /zones
  } else if (m == "GET" && p == "/zones") {
    sendResponse(client, 200, zonesJson());

  // POST /zones/{id}/on|off
  } else if (m == "POST" && p.startsWith("/zones/")) {
    routeZoneControl(client, req);

  // PUT /zones/{id} — update zone settings
  } else if (m == "PUT" && p.startsWith("/zones/")) {
    int id = p.substring(7).toInt() - 1;
    if (id < 0 || id >= ZONE_COUNT) {
      sendResponse(client, 404, "{\"error\":\"zone not found\"}"); return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
      sendResponse(client, 400, "{\"error\":\"invalid JSON body\"}"); return;
    }
    if (!doc["enabled"].isNull()) {
      zoneEnabled[id] = doc["enabled"].as<bool>();
      if (!zoneEnabled[id]) setZone(id, false);  // turn off immediately if disabled
      needsRedraw = true;
      wakeDisplay();
    }
    if (!doc["rate"].isNull()) {
      int tenths = (int)round(doc["rate"].as<float>() * 10.0f);
      if (tenths < 1 || tenths > 50) {
        sendResponse(client, 400, "{\"error\":\"rate must be 0.1–5.0 in/hr\"}"); return;
      }
      zoneRate[id] = tenths;
    }
    char rateBuf[6];
    formatRate(zoneRate[id], rateBuf, sizeof(rateBuf));
    sendResponse(client, 200,
      "{\"id\":"      + String(id + 1) +
      ",\"enabled\":" + (zoneEnabled[id] ? "true" : "false") +
      ",\"rate\":"    + rateBuf + "}");

  // POST /command — generic device command
  } else if (m == "POST" && p == "/command") {
    routeCommand(client, req);

  // GET /jobs/run — run status. Checked before /jobs/{id} so "run" is not
  // mistaken for an id.
  } else if (m == "GET" && isJobPath && tail == "/run") {
    int n = runStateJson(_jobBuf, sizeof(_jobBuf));
    sendResponseRaw(client, 200, _jobBuf, n);

  // GET /jobs/missed — what the device slept through
  } else if (m == "GET" && isJobPath && tail == "/missed") {
    int n = missed::toJson(_jobBuf, sizeof(_jobBuf));
    sendResponseRaw(client, 200, _jobBuf, n);

  // POST /jobs/stop — stop whatever is running
  } else if (m == "POST" && isJobPath && tail == "/stop") {
    stopJob();
    needsRedraw = true;
    wakeDisplay();
    sendResponse(client, 200, "{\"stopped\":true}");

  // GET /jobs — list every job
  } else if (m == "GET" && isJobPath && tail.length() == 0) {
    _sendJobList(client);

  // POST /jobs — ADD a job. Unlike the old single-schedule firmware this does
  // not replace anything.
  } else if (m == "POST" && isJobPath && tail.length() == 0) {
    Job staged;
    jobInitDefaults(staged);
    if (!parseJobBody(req.body, staged, /*requireSteps=*/true)) {
      sendResponse(client, 400, "{\"error\":\"invalid body\"}"); return;
    }
    Job* slot = allocJob();
    if (!slot) {
      sendResponse(client, 507, "{\"error\":\"job storage full\"}"); return;
    }
    staged.used = true;
    staged.id   = nextJobId++;
    *slot = staged;
    _persistAndRefresh();
    _sendJob(client, 201, *slot);

  // POST /jobs/{id}/run — run immediately
  } else if (m == "POST" && isJobPath && tail.endsWith("/run")) {
    int id = _idFromPath(p, _prefixLen(p));
    Job* j = findJob(id);
    if (!j) { sendResponse(client, 404, "{\"error\":\"job not found\"}"); return; }
    if (!startJob(*j)) {
      sendResponse(client, 409, "{\"error\":\"job has no runnable zones\"}"); return;
    }
    needsRedraw = true;
    wakeDisplay();
    sendResponse(client, 200,
      "{\"started\":true,\"job_id\":" + String(j->id) +
      ",\"schedule_id\":" + String(j->id) + "}");

  // GET /jobs/{id}
  } else if (m == "GET" && isJobPath && tail.length() > 1) {
    Job* j = findJob(_idFromPath(p, _prefixLen(p)));
    if (!j) { sendResponse(client, 404, "{\"error\":\"job not found\"}"); return; }
    _sendJob(client, 200, *j);

  // PUT /jobs/{id} — partial update. Setting "repeat" here is what turns a
  // run command into a schedule.
  } else if (m == "PUT" && isJobPath && tail.length() > 1) {
    Job* j = findJob(_idFromPath(p, _prefixLen(p)));
    if (!j) { sendResponse(client, 404, "{\"error\":\"job not found\"}"); return; }
    Job staged = *j;   // start from current values so absent keys are untouched
    if (!parseJobBody(req.body, staged, /*requireSteps=*/false)) {
      sendResponse(client, 400, "{\"error\":\"invalid body\"}"); return;
    }
    *j = staged;
    _persistAndRefresh();
    _sendJob(client, 200, *j);

  // DELETE /jobs/{id}
  } else if (m == "DELETE" && isJobPath && tail.length() > 1) {
    Job* j = findJob(_idFromPath(p, _prefixLen(p)));
    if (!j) { sendResponse(client, 404, "{\"error\":\"job not found\"}"); return; }
    if (jobIsRunning(*j)) stopJob();
    jobInitDefaults(*j);
    _persistAndRefresh();
    sendResponse(client, 200, "{\"deleted\":true}");

  } else {
    sendResponse(client, 404, "{\"error\":\"not found\"}");
  }
}
