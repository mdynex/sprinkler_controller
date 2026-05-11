#pragma once
#include <WiFiUDP.h>
#include "config.h"

// ── Internal state ─────────────────────────────────────────────────────────
static WiFiUDP        _ntpUDP;
static unsigned long  _epochAtSync = 0;   // UTC epoch at last sync
static unsigned long  _millisAtSync = 0;  // millis() at last sync
static unsigned long  _lastSyncAttempt = 0;

bool ntpReady = false;

// ── NTP packet ─────────────────────────────────────────────────────────────
static bool _ntpRequest() {
  byte pkt[48] = {0};
  pkt[0] = 0b11100011;  // LI=3, Version=4, Mode=3 (client)
  pkt[1] = 0;
  pkt[2] = 6;
  pkt[3] = 0xEC;
  pkt[12] = 49;
  pkt[13] = 0x4E;
  pkt[14] = 49;
  pkt[15] = 52;

  _ntpUDP.beginPacket("pool.ntp.org", 123);
  _ntpUDP.write(pkt, 48);
  _ntpUDP.endPacket();

  unsigned long t = millis();
  while (millis() - t < 2000) {
    if (_ntpUDP.parsePacket() >= 44) {
      _ntpUDP.read(pkt, 48);
      unsigned long hi    = (unsigned long)pkt[40] << 8 | pkt[41];
      unsigned long lo    = (unsigned long)pkt[42] << 8 | pkt[43];
      unsigned long epoch = (hi << 16 | lo) - 2208988800UL;  // convert NTP to Unix epoch
      _epochAtSync  = epoch;
      _millisAtSync = millis();
      return true;
    }
    delay(10);
  }
  return false;
}

// ── Current epoch (local time) ─────────────────────────────────────────────
static unsigned long _nowEpoch() {
  unsigned long elapsed = (millis() - _millisAtSync) / 1000UL;
  return _epochAtSync + elapsed + UTC_OFFSET_SECONDS;
}

// ── Public API ─────────────────────────────────────────────────────────────

void ntpInit() {
  _ntpUDP.begin(2390);
  if (_ntpRequest()) {
    ntpReady = true;
    _lastSyncAttempt = millis();
    Serial.print("Time synced: ");
    Serial.println(_nowEpoch());
  } else {
    Serial.println("NTP sync failed — will retry in loop()");
  }
}

// Call every loop() — re-syncs once per hour.
void ntpUpdate() {
  if (millis() - _lastSyncAttempt < 3600000UL) return;
  _lastSyncAttempt = millis();
  if (_ntpRequest()) ntpReady = true;
}

int ntpHour()   { return (int)((_nowEpoch() % 86400UL) / 3600UL); }
int ntpMinute() { return (int)((_nowEpoch() % 3600UL)  / 60UL); }
int ntpSecond() { return (int)(_nowEpoch() % 60UL); }

unsigned long ntpEpoch() { return _nowEpoch(); }

// Returns "HH:MM:SS"
String ntpFormattedTime() {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ntpHour(), ntpMinute(), ntpSecond());
  return String(buf);
}

String currentTimeJson() {
  if (!ntpReady) return "{\"synced\":false}";
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ntpHour(), ntpMinute(), ntpSecond());
  return "{\"synced\":true,\"time\":\"" + String(buf) + "\",\"epoch\":" + String(_nowEpoch()) + "}";
}
