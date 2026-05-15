#pragma once
#include "http_utils.h"
#include "zones.h"
#include "schedules.h"
#include "ntp.h"

extern bool needsRedraw;   // defined in display.h — set true to trigger full screen refresh
extern void wakeDisplay(); // defined in display.h — wakes backlight and resets sleep timer

// =============================================================================
// api.h — REST API route handlers
//
// All incoming HTTP requests are dispatched here by handleApi(). Each route
// is matched by HTTP method + URL path. Responses are always JSON.
//
// Full API reference:
//
//   GET  /time                   → current synced time and epoch
//   GET  /zones                  → list all zones and their on/off state
//   POST /zones/{id}/on          → turn zone {id} on  (id = 1–8)
//   POST /zones/{id}/off         → turn zone {id} off
//
//   GET  /schedules              → list all schedules
//   POST /schedules              → create a new schedule (JSON body required)
//   GET  /schedules/{id}         → get one schedule by id
//   PUT  /schedules/{id}         → update a schedule (JSON body required)
//   DELETE /schedules/{id}       → delete a schedule
//   POST /schedules/{id}/run     → start running a schedule immediately
//   POST /schedules/stop         → stop whichever schedule is running
//   GET  /schedules/run          → get current run status
//
// Schedule JSON body format (for POST and PUT):
//   {
//     "name":     "Morning",       required
//     "auto_run": true,            optional, default false
//     "run_time": "06:00",         optional, 24h HH:MM, default "06:00"
//     "zones": [                   required, at least one entry
//       { "zone": 1, "duration": 300 },   zone 1-8, duration in seconds
//       { "zone": 3, "duration": 600 }
//     ]
//   }
// =============================================================================

// Handle POST /zones/{id}/on|off
// Parses the zone id and action from the path, validates them, and toggles
// the relay pin via setZone().
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

// Handle POST /schedules/{id}/run
// Extracts the schedule id from between "/schedules/" and "/run".
// Since there is only one schedule, the id is accepted as long as it matches.
void routeRunSchedule(WiFiClient& client, const String& path) {
  int id = path.substring(11, path.length() - 4).toInt();
  if (!theSchedule.used || theSchedule.id != id) {
    sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return;
  }
  startSchedule();
  needsRedraw = true;
  wakeDisplay();
  sendResponse(client, 200,
    "{\"started\":true,\"schedule_id\":" + String(theSchedule.id) + "}");
}

// Main request dispatcher — called for every incoming HTTP request.
void handleApi(WiFiClient& client, HttpRequest& req) {
  String& m = req.method;
  String& p = req.path;

  // GET /time — returns current synced time
  if (m == "GET" && p == "/time") {
    sendResponse(client, 200, currentTimeJson());

  // GET /zones — list all zones
  } else if (m == "GET" && p == "/zones") {
    sendResponse(client, 200, zonesJson());

  // POST /zones/{id}/on|off — control a single zone
  } else if (m == "POST" && p.startsWith("/zones/")) {
    routeZoneControl(client, req);

  // PUT /zones/{id} — update zone settings (rate only for now)
  } else if (m == "PUT" && p.startsWith("/zones/")) {
    int id  = p.substring(7).toInt() - 1;
    if (id < 0 || id >= ZONE_COUNT) {
      sendResponse(client, 404, "{\"error\":\"zone not found\"}"); return;
    }
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
      sendResponse(client, 400, "{\"error\":\"invalid JSON body\"}"); return;
    }
    if (doc.containsKey("enabled")) {
      zoneEnabled[id] = doc["enabled"].as<bool>();
      if (!zoneEnabled[id]) setZone(id, false);  // turn off immediately if disabled
      needsRedraw = true;
      wakeDisplay();
    }
    if (doc.containsKey("rate")) {
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

  // GET /schedules — return the one schedule (or an empty array)
  } else if (m == "GET" && p == "/schedules") {
    sendResponse(client, 200, schedulesListJson());

  // GET /schedules/run — current run status (checked before /schedules/{id}
  // so "run" is not mistaken for a numeric id)
  } else if (m == "GET" && p == "/schedules/run") {
    sendResponse(client, 200, runStateJson());

  // GET /schedules/{id} — get the schedule (id must match)
  } else if (m == "GET" && p.startsWith("/schedules/")) {
    int id = p.substring(11).toInt();
    if (!theSchedule.used || theSchedule.id != id) {
      sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return;
    }
    sendResponse(client, 200, scheduleToJson(theSchedule));

  // POST /schedules — replace the current schedule (stops any running schedule)
  } else if (m == "POST" && p == "/schedules") {
    Schedule s = {};
    if (!parseScheduleBody(req.body, s)) {
      sendResponse(client, 400, "{\"error\":\"invalid body\"}"); return;
    }
    if (runState.running) stopSchedule();
    s.used = true;
    s.id   = nextScheduleId++;
    theSchedule = s;
    sendResponse(client, 201, scheduleToJson(theSchedule));

  // PUT /schedules/{id} — update the schedule in-place (id must match)
  } else if (m == "PUT" && p.startsWith("/schedules/")) {
    int id = p.substring(11).toInt();
    if (!theSchedule.used || theSchedule.id != id) {
      sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return;
    }
    Schedule s = theSchedule;  // start from existing values so partial updates work
    if (!parseScheduleBody(req.body, s)) {
      sendResponse(client, 400, "{\"error\":\"invalid body\"}"); return;
    }
    theSchedule = s;
    sendResponse(client, 200, scheduleToJson(theSchedule));

  // DELETE /schedules/{id} — remove the schedule (id must match)
  } else if (m == "DELETE" && p.startsWith("/schedules/")) {
    int id = p.substring(11).toInt();
    if (!theSchedule.used || theSchedule.id != id) {
      sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return;
    }
    if (runState.running) stopSchedule();
    theSchedule.used = false;
    sendResponse(client, 200, "{\"deleted\":true}");

  // POST /schedules/{id}/run — start the schedule immediately (id must match)
  } else if (m == "POST" && p.startsWith("/schedules/") && p.endsWith("/run")) {
    routeRunSchedule(client, p);

  // POST /schedules/stop — stop whatever is currently running
  } else if (m == "POST" && p == "/schedules/stop") {
    stopSchedule();
    needsRedraw = true;
    wakeDisplay();
    sendResponse(client, 200, "{\"stopped\":true}");

  } else {
    sendResponse(client, 404, "{\"error\":\"not found\"}");
  }
}
