#pragma once
#include <WiFiUDP.h>
#include "config.h"

// =============================================================================
// ntp.h — Network Time Protocol (NTP) client
//
// Fetches the current UTC time from pool.ntp.org over UDP immediately after
// WiFi connects, then re-syncs automatically once per hour.
//
// No external library is required — the NTP packet is built and parsed
// manually using the built-in WiFiUDP class.
//
// How it works:
//   1. Send a 48-byte NTP request packet to pool.ntp.org on port 123.
//   2. The server replies with a packet containing the current UTC timestamp
//      in the 32-bit "Transmit Timestamp" field at bytes 40–43.
//   3. Convert from NTP epoch (Jan 1 1900) to Unix epoch (Jan 1 1970) by
//      subtracting 70 years worth of seconds (2208988800).
//   4. Add UTC_OFFSET_SECONDS from config.h to get local time.
//   5. Between syncs, advance the clock using millis() so it stays accurate
//      without hitting the server every second.
// =============================================================================

static WiFiUDP        _ntpUDP;
static unsigned long  _epochAtSync  = 0;   // Unix epoch (local) at last successful sync
static unsigned long  _millisAtSync = 0;   // millis() value at last successful sync
static unsigned long  _lastSyncAttempt = 0;

bool ntpReady = false;  // true once the first successful sync completes

// Send one NTP request and wait up to 2 seconds for a reply.
// Returns true and updates _epochAtSync on success.
static bool _ntpRequest() {
  byte pkt[48] = {0};
  // NTP request header: LI=3 (unknown), Version=4, Mode=3 (client)
  pkt[0] = 0b11100011;
  pkt[1] = 0;     // stratum (unspecified)
  pkt[2] = 6;     // polling interval (64 seconds)
  pkt[3] = 0xEC;  // peer clock precision

  // Reference identifier (not used by the server for requests)
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
      // Transmit Timestamp is at bytes 40–43 (two 16-bit words, big-endian)
      unsigned long hi    = (unsigned long)pkt[40] << 8 | pkt[41];
      unsigned long lo    = (unsigned long)pkt[42] << 8 | pkt[43];
      unsigned long epoch = (hi << 16 | lo) - 2208988800UL;  // NTP → Unix epoch
      _epochAtSync  = epoch;
      _millisAtSync = millis();
      return true;
    }
    delay(10);
  }
  return false;
}

// Returns the current local Unix epoch, estimated from the last sync plus
// elapsed millis() so we don't need to hit the server every second.
static unsigned long _nowEpoch() {
  unsigned long elapsed = (millis() - _millisAtSync) / 1000UL;
  return _epochAtSync + elapsed + UTC_OFFSET_SECONDS;
}

// ── Public API ─────────────────────────────────────────────────────────────

// Call once after WiFi connects. Opens the UDP socket and does the first sync.
void ntpInit() {
  _ntpUDP.begin(2390);  // local port for receiving NTP replies
  if (_ntpRequest()) {
    ntpReady = true;
    _lastSyncAttempt = millis();
    Serial.print("Time synced: ");
    Serial.println(_nowEpoch());
  } else {
    Serial.println("NTP sync failed — will retry in loop()");
  }
}

// Call every loop(). Re-syncs with the server once per hour.
void ntpUpdate() {
  if (millis() - _lastSyncAttempt < 3600000UL) return;
  _lastSyncAttempt = millis();
  if (_ntpRequest()) ntpReady = true;
}

// Current local hour (0–23), minute (0–59), second (0–59).
int ntpHour()   { return (int)((_nowEpoch() % 86400UL) / 3600UL); }
int ntpMinute() { return (int)((_nowEpoch() % 3600UL)  / 60UL); }
int ntpSecond() { return (int)(_nowEpoch() % 60UL); }

// Current local time as a Unix epoch timestamp.
unsigned long ntpEpoch() { return _nowEpoch(); }

// Current local time as a "HH:MM:SS" string.
String ntpFormattedTime() {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ntpHour(), ntpMinute(), ntpSecond());
  return String(buf);
}

// JSON response for GET /time.
String currentTimeJson() {
  if (!ntpReady) return "{\"synced\":false}";
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ntpHour(), ntpMinute(), ntpSecond());
  return "{\"synced\":true,\"time\":\"" + String(buf) + "\",\"epoch\":" + String(_nowEpoch()) + "}";
}
