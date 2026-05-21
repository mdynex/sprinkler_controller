#pragma once

// =============================================================================
// state.h — Shared device state declarations
//
// Defined (with storage) in sprinkler-controller.ino. Other modules
// (api.h, pairing.h, heartbeat.h, display.h) include this header to read
// or mutate the active state.
// =============================================================================

enum DeviceState {
  STATE_WIFI_SETUP,   // No WiFi creds (or repeated connect failure)
  STATE_PAIRING,      // WiFi up, but no server config — waiting for the app
  STATE_RUNNING       // Fully provisioned and operational
};

extern DeviceState deviceState;

// Server config — populated from KVStore at boot or from POST /provision
extern char serverHost[64];
extern char deviceId[37];
extern char deviceToken[65];

// One-shot claim secret + derived 6-digit code, generated each PAIRING entry
extern char claimSecret[17];
extern char claimCode[7];
