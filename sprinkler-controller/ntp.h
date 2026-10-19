#pragma once
#include <WiFiUDP.h>
#include "config.h"
#include "tz.h"

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
//   4. Convert to local time with the TZ_RULE from config.h, which knows
//      about daylight saving. Without a TZ_RULE the fixed UTC_OFFSET_SECONDS
//      is used (no DST — the old behaviour).
//   5. Between syncs, advance the clock using millis() so it stays accurate
//      without hitting the server every second.
//
// "Local epoch" (ntpEpoch(), job run_at/last_run) means local wall-clock time
// counted as if it were UTC. The server relies on that, so it's unchanged;
// only the offset now follows DST.
// =============================================================================

static WiFiUDP        _ntpUDP;
static unsigned long  _epochAtSync  = 0;   // Unix epoch (UTC) at last successful sync
static unsigned long  _millisAtSync = 0;   // millis() value at last successful sync
static unsigned long  _lastSyncAttempt = 0;

// Retry quickly until the first sync lands — nothing time-based (jobs, the
// missed-job scan, the clock) works without it — then hourly. A failed
// hourly resync tries again sooner, since the clock is still drifting.
static const unsigned long NTP_RETRY_UNSYNCED_MS = 60000UL;      // 1 min
static const unsigned long NTP_RETRY_FAILED_MS   = 300000UL;     // 5 min
static const unsigned long NTP_RESYNC_MS         = 3600000UL;    // 1 hour
static unsigned long       _nextSyncDelay        = NTP_RETRY_UNSYNCED_MS;

bool ntpReady = false;  // true once the first successful sync completes

// Time zone: a POSIX TZ rule (with DST) if config.h defines TZ_RULE,
// otherwise the fixed offset. Parsed once, on first use.
static TzInfo _tz;
static bool   _tzLoaded = false;

static const TzInfo& _tzInfo() {
  if (!_tzLoaded) {
    _tzLoaded = true;
    _tz = {};
    _tz.stdOffset = (int32_t)UTC_OFFSET_SECONDS;
#ifdef TZ_RULE
    if (!tzParse(TZ_RULE, &_tz)) {
      _tz = {};
      _tz.stdOffset = (int32_t)UTC_OFFSET_SECONDS;
      Serial.println("[ntp] TZ_RULE not understood - using UTC_OFFSET_SECONDS (no DST)");
    }
#endif
  }
  return _tz;
}

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
      // Ignore anything that isn't a sane server reply (mode 4, a real
      // stratum, a non-zero time) — a bad packet would move every schedule.
      if ((pkt[0] & 0x07) != 4 || pkt[1] == 0 || (hi | lo) == 0) { delay(10); continue; }
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
static unsigned long _nowUtc() {
  return _epochAtSync + (millis() - _millisAtSync) / 1000UL;
}

// Offset from UTC in effect right now, in seconds (DST-aware).
long ntpUtcOffset() {
  return (long)tzOffsetAt(_tzInfo(), (int64_t)_nowUtc());
}

static unsigned long _nowEpoch() {
  return _nowUtc() + ntpUtcOffset();
}

// ── Public API ─────────────────────────────────────────────────────────────

// Call once after WiFi connects. Opens the UDP socket and does the first sync.
void ntpInit() {
  _ntpUDP.begin(2390);  // local port for receiving NTP replies
  _lastSyncAttempt = millis();
  if (_ntpRequest()) {
    ntpReady = true;
    _nextSyncDelay = NTP_RESYNC_MS;
    Serial.print("Time synced: ");
    Serial.print(_nowEpoch());
    Serial.print("  utc_offset=");
    Serial.println(ntpUtcOffset());
  } else {
    _nextSyncDelay = NTP_RETRY_UNSYNCED_MS;
    Serial.println("NTP sync failed — retrying every minute");
  }
}

// Call every loop(). Retries every minute until the first sync, then
// re-syncs hourly (every 5 minutes after a failed resync).
void ntpUpdate() {
  if (millis() - _lastSyncAttempt < _nextSyncDelay) return;
  _lastSyncAttempt = millis();
  if (_ntpRequest()) {
    if (!ntpReady) Serial.println("[ntp] First sync done");
    ntpReady = true;
    _nextSyncDelay = NTP_RESYNC_MS;
  } else {
    _nextSyncDelay = ntpReady ? NTP_RETRY_FAILED_MS : NTP_RETRY_UNSYNCED_MS;
  }
}

// Current local hour (0–23), minute (0–59), second (0–59).
int ntpHour()   { return (int)((_nowEpoch() % 86400UL) / 3600UL); }
int ntpMinute() { return (int)((_nowEpoch() % 3600UL)  / 60UL); }
int ntpSecond() { return (int)(_nowEpoch() % 60UL); }

// Current local day of week (0 = Sunday, 6 = Saturday).
// Jan 1 1970 was a Thursday → day index 4.
int ntpDayOfWeek() { return (int)(((_nowEpoch() / 86400UL) + 4UL) % 7UL); }

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
  return "{\"synced\":true,\"time\":\"" + String(buf) + "\",\"epoch\":" + String(_nowEpoch()) +
         ",\"utc_offset\":" + String(ntpUtcOffset()) + "}";
}
