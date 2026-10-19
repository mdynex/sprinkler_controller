#pragma once
#include <WiFi.h>
#include "mbed.h"          // mbed_stats_heap_get — heap diagnostic
#include "state.h"
#include "missed.h"
#include "wifi_recovery.h"

// =============================================================================
// heartbeat.h — Periodic keepalive to the Home AI Server
//
// Calls POST /devices/heartbeat once per minute using the device token saved
// during provisioning. The body includes the Arduino's current LAN address
// so DHCP changes are reflected on the server without a full re-pair.
//
// Authentication is X-Device-Token only — the Arduino does NOT hold the
// server's master API key. Only valid while `deviceState == STATE_RUNNING`
// (and after `serverHost` + `deviceToken` are populated).
//
// Call heartbeatTick() from loop(); it's non-blocking and bails out if:
//   - not yet in STATE_RUNNING
//   - no server config loaded
//   - the interval hasn't elapsed
// =============================================================================

static unsigned long _lastHeartbeat = 0;
static const unsigned long HB_INTERVAL_MS = 60000UL;  // 1 minute

// Split "host:port" → ("host", port).
// Falls back to port 8000 if the input has no colon.
static void _parseHostPort(const char* hostPort, char* hostOut, size_t hostLen, int* portOut) {
  const char* colon = strchr(hostPort, ':');
  if (!colon) {
    strncpy(hostOut, hostPort, hostLen - 1);
    hostOut[hostLen - 1] = '\0';
    *portOut = 8000;
    return;
  }
  size_t hlen = (size_t)(colon - hostPort);
  if (hlen >= hostLen) hlen = hostLen - 1;
  memcpy(hostOut, hostPort, hlen);
  hostOut[hlen] = '\0';
  *portOut = atoi(colon + 1);
  if (*portOut <= 0) *portOut = 8000;
}

// Largest single block malloc will still hand out, found by binary search.
// This is the direct fragmentation signal and — unlike mbed_stats_heap_get —
// it works no matter how the core was built: if free memory has fragmented
// into small islands, this number shrinks even while total free bytes look
// healthy. The block is freed immediately, so the probe is non-destructive.
static size_t _largestFreeBlock() {
  size_t lo = 0, hi = 512UL * 1024UL;   // search 0..512 KB
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    void* p = malloc(mid);
    if (p) { free(p); lo = mid; }
    else   { hi = mid - 1; }
  }
  return lo;
}

// Periodic heap log — diagnostic for the Arduino-String fragmentation theory.
// Runs once per minute regardless of pairing state, so it captures the heap
// trend even before the device is provisioned. Watch the serial log over a few
// hours of uptime:
//   • largestBlock shrinking over time     → fragmentation and/or a leak; this
//                                             is the freeze mechanism
//   • mbed `current` climbing steadily      → a straight leak
//   • mbed values all 0                     → heap stats aren't compiled into
//                                             your core; rely on largestBlock
// Safe to delete once the cause is settled.
void heapLogTick() {
  static unsigned long _lastHeapLog = 0;
  unsigned long now = millis();
  if (now - _lastHeapLog < HB_INTERVAL_MS) return;
  _lastHeapLog = now;

  mbed_stats_heap_t hs;
  mbed_stats_heap_get(&hs);
  Serial.print("[heap] largestBlock="); Serial.print(_largestFreeBlock());
  Serial.print(" current=");            Serial.print(hs.current_size);
  Serial.print(" reserved=");           Serial.print(hs.reserved_size);
  Serial.print(" allocFails=");         Serial.println(hs.alloc_fail_cnt);
}

void heartbeatTick() {
  if (deviceState != STATE_RUNNING)            return;
  if (deviceToken[0] == '\0')                  return;
  if (serverHost[0]  == '\0')                  return;

  unsigned long now = millis();
  if (now - _lastHeartbeat < HB_INTERVAL_MS) return;
  _lastHeartbeat = now;

  char host[64]; int port = 8000;
  _parseHostPort(serverHost, host, sizeof(host), &port);

  WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("[heartbeat] Cannot reach server");
    // Signal wifi_recovery so a wedged-but-status-OK radio still triggers
    // a silent reconnect after a few consecutive misses. Server-down (vs.
    // wifi-down) is indistinguishable here, but the worst case is a
    // harmless redundant reconnect once the threshold is hit.
    wifi_recovery::noteHeartbeatFailure();
    return;
  }

  String ip = WiFi.localIP().toString();

  // Body carries the LAN address, plus any jobs the device slept through since
  // the last successful report. Built in a static buffer — this runs every
  // minute forever, so it must not churn the heap.
  // 3 KB fits a full missed-job buffer (8 records with escaped names).
  static char body[3072];
  int n = snprintf(body, sizeof(body), "{\"address\":\"%s:80\"", ip.c_str());
  int reported = missed::count();
  if (reported > 0 && (size_t)n < sizeof(body)) {
    int w = snprintf(body + n, sizeof(body) - n, ",\"missed_jobs\":");
    n += w;
    int m = missed::toJson(body + n, sizeof(body) - n);
    if (m > 0) {
      n += m;
    } else {
      n -= w;          // didn't fit — send the plain heartbeat, retry next time
      reported = 0;
    }
  }
  if ((size_t)n < sizeof(body) - 2) {
    n += snprintf(body + n, sizeof(body) - n, "}");
  }

  client.println("POST /devices/heartbeat HTTP/1.1");
  client.print("Host: ");           client.println(host);
  client.print("X-Device-Token: "); client.println(deviceToken);
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(n);
  client.println("Connection: close");
  client.println();
  client.write((const uint8_t*)body, n);

  // Read the status line so we only clear the missed-job buffer once the
  // server has actually accepted the report. Dropping it on a 5xx would lose
  // the only notification the user gets that watering was skipped.
  unsigned long start = millis();
  while (!client.available() && millis() - start < 3000) delay(10);
  bool gotResponse = client.available();

  bool accepted = false;
  if (gotResponse) {
    char status[64] = {0};
    size_t i = 0;
    while (client.available() && i < sizeof(status) - 1) {
      char c = client.read();
      if (c == '\n') break;
      status[i++] = c;
    }
    // "HTTP/1.1 200 OK" — accept any 2xx.
    const char* sp = strchr(status, ' ');
    if (sp && sp[1] == '2') accepted = true;
  }
  client.stop();

  if (accepted && reported > 0) {
    Serial.print("[heartbeat] Reported "); Serial.print(reported);
    Serial.println(" missed job(s)");
    missed::clear();
  }

  if (gotResponse) {
    wifi_recovery::noteHeartbeatSuccess();
  } else {
    // Connect succeeded but no bytes came back within 3 s — server is up
    // enough to ACK SYN but not enough to respond. Treat as a soft failure
    // so the recovery tier still escalates after repeated misses.
    Serial.println("[heartbeat] No response from server");
    wifi_recovery::noteHeartbeatFailure();
  }
}
