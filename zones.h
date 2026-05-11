#pragma once
#include "config.h"

bool zoneState[ZONE_COUNT] = {false};

void zonesInit() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    pinMode(ZONE_PINS[i], OUTPUT);
    digitalWrite(ZONE_PINS[i], HIGH);  // HIGH = off on a low-trigger relay
  }
}

void setZone(int index, bool on) {
  if (index < 0 || index >= ZONE_COUNT) return;
  zoneState[index] = on;
  digitalWrite(ZONE_PINS[index], on ? LOW : HIGH);  // LOW = on, HIGH = off
}

void allZonesOff() {
  for (int i = 0; i < ZONE_COUNT; i++) setZone(i, false);
}

String zonesJson() {
  String json = "[";
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(i + 1) + ",\"state\":\"" + (zoneState[i] ? "on" : "off") + "\"}";
  }
  json += "]";
  return json;
}
