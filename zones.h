#pragma once
#include "config.h"

// =============================================================================
// zones.h — Zone state tracking and relay pin control
//
// The relay module is LOW-TRIGGER (active low):
//   digitalWrite(pin, LOW)  → relay energizes → valve OPENS  → zone ON
//   digitalWrite(pin, HIGH) → relay releases  → valve CLOSES → zone OFF
//
// All pins are initialised HIGH (off) at startup so no valves open
// accidentally before the board has finished booting.
// =============================================================================

// Current on/off state for each zone (index 0 = Zone 1, etc.)
bool zoneState[ZONE_COUNT] = {false};

// Set all relay pins to HIGH (off) and configure them as outputs.
void zonesInit() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    pinMode(ZONE_PINS[i], OUTPUT);
    digitalWrite(ZONE_PINS[i], HIGH);  // HIGH = off on a low-trigger relay
  }
}

// Turn a single zone on or off. index is 0-based.
void setZone(int index, bool on) {
  if (index < 0 || index >= ZONE_COUNT) return;
  zoneState[index] = on;
  digitalWrite(ZONE_PINS[index], on ? LOW : HIGH);
}

// Turn every zone off immediately. Called when a schedule finishes or is stopped.
void allZonesOff() {
  for (int i = 0; i < ZONE_COUNT; i++) setZone(i, false);
}

// Build a JSON array of all zones and their current state, e.g.:
// [{"id":1,"state":"off"},{"id":2,"state":"on"}, ...]
String zonesJson() {
  String json = "[";
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(i + 1) + ",\"state\":\"" + (zoneState[i] ? "on" : "off") + "\"}";
  }
  json += "]";
  return json;
}
