#pragma once
#include <WiFi.h>
#include "state.h"
#include "storage.h"
#include "wifi_setup.h"

// =============================================================================
// wifi_recovery.h — Silent WiFi reconnect tier before the setup wizard
//
// Background
// ----------
// The Giga's Murata 1DX WiFi module occasionally wedges after long uptime:
// `WiFi.status()` lies (still returns WL_CONNECTED) while no packets flow,
// or the link truly drops and the driver never reconnects on its own. The
// previous watchdog reacted to "disconnected > 60 s" by opening the on-screen
// setup wizard — fine for cold boot, terrible for a runtime blip because it
// requires a human to tap through. A real-world log showed the controller
// stuck offline for 4.5 hours because no one was around.
//
// This module sits between the symptom (no successful network I/O) and the
// nuclear option (setup wizard). It performs up to 3 silent reconnect
// attempts using the saved credentials, with exponential backoff between
// them. Only if all of those fail does the caller fall back to the wizard.
//
// Triggers
// --------
//   1. `WiFi.status() != WL_CONNECTED`   — driver admits disconnect.
//   2. >= HEARTBEAT_FAIL_THRESHOLD consecutive heartbeat failures —
//      catches the wedged-stack case where status() lies.
//
// Public API
// ----------
//   noteHeartbeatSuccess() — heartbeat round-trip OK, reset counter
//   noteHeartbeatFailure() — heartbeat couldn't reach server
//   tick()                 — drive the state machine; call from loop()
//   exhausted()            — true when the silent tier has given up;
//                            caller should now invoke wifi_setup::run()
//   reset()                — call after the wizard runs (or to clear state)
// =============================================================================

namespace wifi_recovery {

  // ── Tunables ─────────────────────────────────────────────────────────────
  static const int           RECONNECT_ATTEMPT_TIMEOUT_MS = 8000;
  static const int           MAX_SILENT_ATTEMPTS          = 3;
  static const int           HEARTBEAT_FAIL_THRESHOLD     = 3;
  // Backoff between attempts. Length must be >= MAX_SILENT_ATTEMPTS - 1.
  static const unsigned long BACKOFF_MS[]                 = {10000UL, 30000UL, 60000UL};

  // ── Internal state ───────────────────────────────────────────────────────
  static int           _hbFailures     = 0;
  static int           _silentAttempts = 0;
  static unsigned long _nextAttemptAt  = 0;
  static bool          _recovering     = false;
  static bool          _exhausted      = false;

  // Heartbeat reports — kept inline so heartbeat.h can drop them on the
  // fast path with no function-call overhead.
  inline void noteHeartbeatSuccess() { _hbFailures = 0; }
  inline void noteHeartbeatFailure() { _hbFailures++;   }

  inline bool exhausted() { return _exhausted; }

  // Reset all recovery state. Call this after the wizard returns so the
  // next runtime blip starts fresh on the silent tier.
  inline void reset() {
    _hbFailures     = 0;
    _silentAttempts = 0;
    _nextAttemptAt  = 0;
    _recovering     = false;
    _exhausted      = false;
  }

  // Synchronous best-effort reconnect using the saved creds. Blocks for up
  // to RECONNECT_ATTEMPT_TIMEOUT_MS, returning true the moment WL_CONNECTED
  // is observed. Failure leaves the radio disconnected (clean state for
  // the next attempt).
  inline bool _trySilentReconnect() {
    char ssid[33] = "", pw[65] = "";
    if (!storage::loadWifiCreds(ssid, sizeof(ssid), pw, sizeof(pw))) {
      Serial.println("[wifi-recovery] No saved creds — cannot retry silently");
      return false;
    }
    Serial.print("[wifi-recovery] Attempt ");
    Serial.print(_silentAttempts + 1);
    Serial.print("/");
    Serial.print(MAX_SILENT_ATTEMPTS);
    Serial.print(" → ");
    Serial.println(ssid);

    // Tear the link down first. On the Murata module this is the only way
    // to recover from a wedged state where status() still reports CONNECTED
    // but nothing flows.
    WiFi.disconnect();
    delay(200);
    WiFi.begin(ssid, pw);

    unsigned long start = millis();
    while (millis() - start < (unsigned long)RECONNECT_ATTEMPT_TIMEOUT_MS) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[wifi-recovery] Reconnected, IP: ");
        Serial.println(WiFi.localIP());
        return true;
      }
      delay(250);
    }
    Serial.println("[wifi-recovery] Attempt timed out");
    return false;
  }

  // True if recovery should kick in (or keep running).
  inline bool _shouldRecover() {
    if (wifi_setup::isActive())          return false;  // wizard owns the radio
    if (deviceState != STATE_RUNNING)    return false;  // pairing flow handles its own
    if (WiFi.status() != WL_CONNECTED)   return true;
    if (_hbFailures >= HEARTBEAT_FAIL_THRESHOLD) return true;
    return false;
  }

  // Drive the recovery state machine. Returns true while a recovery cycle
  // is in progress (informational — useful if the caller wants to skip
  // other work while we're trying).
  inline bool tick() {
    if (_exhausted) return false;          // caller will handle the wizard

    if (!_recovering) {
      if (!_shouldRecover()) {
        _silentAttempts = 0;
        _nextAttemptAt  = 0;
        return false;
      }
      _recovering     = true;
      _silentAttempts = 0;
      _nextAttemptAt  = millis();          // first attempt fires immediately
      Serial.print("[wifi-recovery] Triggered (hbFails=");
      Serial.print(_hbFailures);
      Serial.print(", status=");
      Serial.print(WiFi.status());
      Serial.println(")");
    }

    if (millis() < _nextAttemptAt) return true;  // sitting in backoff

    if (_trySilentReconnect()) {
      _recovering     = false;
      _silentAttempts = 0;
      _hbFailures     = 0;
      _nextAttemptAt  = 0;
      return false;
    }

    _silentAttempts++;
    if (_silentAttempts >= MAX_SILENT_ATTEMPTS) {
      Serial.println("[wifi-recovery] Silent tier exhausted — caller should open setup wizard");
      _exhausted = true;
      return true;
    }

    // Schedule the next attempt. Clamp the index in case BACKOFF_MS is
    // shorter than MAX_SILENT_ATTEMPTS - 1.
    int boIdx = _silentAttempts - 1;
    const int backoffLen = (int)(sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0]));
    if (boIdx >= backoffLen) boIdx = backoffLen - 1;
    _nextAttemptAt = millis() + BACKOFF_MS[boIdx];
    Serial.print("[wifi-recovery] Next attempt in ");
    Serial.print(BACKOFF_MS[boIdx] / 1000UL);
    Serial.println("s");
    return true;
  }

}  // namespace wifi_recovery
