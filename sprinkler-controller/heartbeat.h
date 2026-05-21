#pragma once
#include <WiFi.h>
#include "state.h"

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
    return;
  }

  String ip   = WiFi.localIP().toString();
  String body = String("{\"address\":\"") + ip + ":80\"}";

  client.println("POST /devices/heartbeat HTTP/1.1");
  client.print("Host: ");           client.println(host);
  client.print("X-Device-Token: "); client.println(deviceToken);
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(body.length());
  client.println("Connection: close");
  client.println();
  client.print(body);

  // Wait briefly for the response, then discard it
  unsigned long start = millis();
  while (!client.available() && millis() - start < 3000) delay(10);
  client.stop();
}
