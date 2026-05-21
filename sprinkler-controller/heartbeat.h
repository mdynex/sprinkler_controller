#pragma once
#include <WiFi.h>
#include "config.h"

// =============================================================================
// heartbeat.h — Periodic keepalive to the Home AI Server
//
// Calls POST /devices/heartbeat once per minute using the device token stored
// by registration.h. The server uses this to update last_seen for the device,
// which is displayed in the Android app's Devices screen.
//
// Uses X-Device-Token (not X-Api-Key) — the server's heartbeat endpoint
// authenticates by device token only.
//
// Call heartbeatTick() from loop(). It is non-blocking and no-ops if:
//   - the device token has not been set (registration not yet complete)
//   - the interval has not elapsed yet
// =============================================================================

extern char deviceToken[65];   // defined in registration.h

static unsigned long _lastHeartbeat   = 0;
static const unsigned long HB_INTERVAL_MS = 60000UL;  // 1 minute

void heartbeatTick() {
  // Not registered yet — nothing to do
  if (deviceToken[0] == '\0') return;

  unsigned long now = millis();
  if (now - _lastHeartbeat < HB_INTERVAL_MS) return;
  _lastHeartbeat = now;

  WiFiClient client;
  if (!client.connect(SERVER_HOST, 8000)) {
    Serial.println("[heartbeat] Cannot reach server");
    return;
  }

  client.println("POST /devices/heartbeat HTTP/1.1");
  client.print("Host: ");           client.println(SERVER_HOST);
  client.print("X-Device-Token: "); client.println(deviceToken);
  client.println("Content-Length: 0");
  client.println("Connection: close");
  client.println();

  // Wait briefly for the response then discard it
  unsigned long start = millis();
  while (!client.available() && millis() - start < 3000) delay(10);
  client.stop();
}
