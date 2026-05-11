#pragma once
#include "http_utils.h"
#include "zones.h"
#include "schedules.h"
#include "ntp.h"

// ── Route: POST /zones/{id}/on|off ─────────────────────────────────────────

void routeZoneControl(WiFiClient& client, HttpRequest& req) {
  String rest  = req.path.substring(7);  // strip "/zones/"
  int    slash = rest.indexOf('/');
  if (slash < 0) { sendResponse(client, 400, "{\"error\":\"missing action\"}"); return; }

  int    id     = rest.substring(0, slash).toInt() - 1;  // 0-based
  String action = rest.substring(slash + 1);

  if (id < 0 || id >= ZONE_COUNT) {
    sendResponse(client, 404, "{\"error\":\"zone not found\"}"); return;
  }
  if (action != "on" && action != "off") {
    sendResponse(client, 400, "{\"error\":\"action must be on or off\"}"); return;
  }
  setZone(id, action == "on");
  sendResponse(client, 200,
    "{\"id\":" + String(id + 1) + ",\"state\":\"" + action + "\"}");
}

// ── Route: POST /schedules/{id}/run ───────────────────────────────────────

void routeRunSchedule(WiFiClient& client, const String& path) {
  String idStr = path.substring(11, path.length() - 4);
  int    idx   = findScheduleIndex(idStr.toInt());
  if (idx < 0) { sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return; }

  startSchedule(idx);
  sendResponse(client, 200,
    "{\"started\":true,\"schedule_id\":" + String(schedules[idx].id) + "}");
}

// ── Main dispatcher ────────────────────────────────────────────────────────

void handleApi(WiFiClient& client, HttpRequest& req) {
  String& m = req.method;
  String& p = req.path;

  // GET /time
  if (m == "GET" && p == "/time") {
    sendResponse(client, 200, currentTimeJson());

  // GET /zones
  } else if (m == "GET" && p == "/zones") {
    sendResponse(client, 200, zonesJson());

  // POST /zones/{id}/on|off
  } else if (m == "POST" && p.startsWith("/zones/")) {
    routeZoneControl(client, req);

  // GET /schedules
  } else if (m == "GET" && p == "/schedules") {
    sendResponse(client, 200, schedulesListJson());

  // GET /schedules/run  — current run status
  } else if (m == "GET" && p == "/schedules/run") {
    sendResponse(client, 200, runStateJson());

  // GET /schedules/{id}
  } else if (m == "GET" && p.startsWith("/schedules/")) {
    int idx = findScheduleIndex(p.substring(11).toInt());
    if (idx < 0) { sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return; }
    sendResponse(client, 200, scheduleToJson(schedules[idx]));

  // POST /schedules  — create
  } else if (m == "POST" && p == "/schedules") {
    int slot = freeSlot();
    if (slot < 0) { sendResponse(client, 400, "{\"error\":\"max schedules reached\"}"); return; }

    Schedule s = {};
    if (!parseScheduleBody(req.body, s)) {
      sendResponse(client, 400, "{\"error\":\"invalid body\"}"); return;
    }
    s.used = true;
    s.id   = nextScheduleId++;
    schedules[slot] = s;
    sendResponse(client, 201, scheduleToJson(schedules[slot]));

  // PUT /schedules/{id}  — update
  } else if (m == "PUT" && p.startsWith("/schedules/")) {
    int id  = p.substring(11).toInt();
    int idx = findScheduleIndex(id);
    if (idx < 0) { sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return; }

    Schedule s = schedules[idx];
    if (!parseScheduleBody(req.body, s)) {
      sendResponse(client, 400, "{\"error\":\"invalid body\"}"); return;
    }
    schedules[idx] = s;
    sendResponse(client, 200, scheduleToJson(schedules[idx]));

  // DELETE /schedules/{id}
  } else if (m == "DELETE" && p.startsWith("/schedules/")) {
    int idx = findScheduleIndex(p.substring(11).toInt());
    if (idx < 0) { sendResponse(client, 404, "{\"error\":\"schedule not found\"}"); return; }
    if (runState.running && runState.scheduleIndex == idx) stopSchedule();
    schedules[idx].used = false;
    sendResponse(client, 200, "{\"deleted\":true}");

  // POST /schedules/{id}/run  — start now
  } else if (m == "POST" && p.startsWith("/schedules/") && p.endsWith("/run")) {
    routeRunSchedule(client, p);

  // POST /schedules/stop
  } else if (m == "POST" && p == "/schedules/stop") {
    stopSchedule();
    sendResponse(client, 200, "{\"stopped\":true}");

  } else {
    sendResponse(client, 404, "{\"error\":\"not found\"}");
  }
}
