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

// Watering rate per zone in tenths of an inch per hour (10 = 1.0 in/hr).
// Range 1–50 (0.1–5.0 in/hr). Default 10 (1.0 in/hr).
int zoneRate[ZONE_COUNT];

// Format a rate value (tenths) as "X.X" into buf (needs at least 5 bytes).
void formatRate(int tenths, char* buf, int bufLen) {
  snprintf(buf, bufLen, "%d.%d", tenths / 10, tenths % 10);
}

// Set all relay pins to HIGH (off), configure them as outputs, and set
// default watering rates.
void zonesInit() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    pinMode(ZONE_PINS[i], OUTPUT);
    digitalWrite(ZONE_PINS[i], HIGH);  // HIGH = off on a low-trigger relay
    zoneRate[i] = 10;                  // default 1.0 in/hr
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

// Build a JSON array of all zones with their state and watering rate, e.g.:
// [{"id":1,"state":"off","rate":1.0}, ...]
String zonesJson() {
  String json = "[";
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (i > 0) json += ",";
    char rate[6];
    formatRate(zoneRate[i], rate, sizeof(rate));
    json += "{\"id\":" + String(i + 1)
          + ",\"state\":\"" + (zoneState[i] ? "on" : "off") + "\""
          + ",\"rate\":" + rate + "}";
  }
  json += "]";
  return json;
}
