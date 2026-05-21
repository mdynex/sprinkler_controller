#pragma once
#include <kvstore_global_api.h>
#include <string.h>

// =============================================================================
// storage.h — Persistent key/value storage on the on-board flash
//
// Wraps the mbed-os global KVStore API so the rest of the code can deal
// with simple bool-returning get/set/clear calls for strings.
//
// Stored values:
//   wifi_ssid, wifi_pw         — set by the WiFi setup wizard
//   srv_host, srv_devid,       — set by the /provision handler after a user
//   srv_token                    claims the device through the app
//
// The values survive power cycles. Calling clearWifiCreds() or
// clearServerConfig() forces the device back through the relevant
// onboarding step on next boot.
// =============================================================================

namespace storage {

namespace {

constexpr const char* K_WIFI_SSID = "wifi_ssid";
constexpr const char* K_WIFI_PW   = "wifi_pw";
constexpr const char* K_SRV_HOST  = "srv_host";
constexpr const char* K_SRV_DEVID = "srv_devid";
constexpr const char* K_SRV_TOKEN = "srv_token";

// Read a null-terminated string. Returns false if the key is missing
// or any read error occurs. `out` is set to "" on failure.
bool _getString(const char* key, char* out, size_t outLen) {
  if (outLen == 0) return false;
  size_t actual = 0;
  int rc = kv_get(key, out, outLen - 1, &actual);
  if (rc != 0) { out[0] = '\0'; return false; }
  if (actual >= outLen) actual = outLen - 1;
  out[actual] = '\0';
  return true;
}

// Store a null-terminated string. We persist strlen+1 bytes so the trailing
// null comes back on read and we never accidentally store junk.
bool _setString(const char* key, const char* val) {
  return kv_set(key, val, strlen(val) + 1, 0) == 0;
}

}  // anonymous namespace


// ── WiFi credentials ───────────────────────────────────────────────────────

bool loadWifiCreds(char* ssid, size_t sLen, char* pw, size_t pLen) {
  bool a = _getString(K_WIFI_SSID, ssid, sLen);
  bool b = _getString(K_WIFI_PW,   pw,   pLen);
  return a && b;
}

bool saveWifiCreds(const char* ssid, const char* pw) {
  return _setString(K_WIFI_SSID, ssid) && _setString(K_WIFI_PW, pw);
}

bool clearWifiCreds() {
  kv_remove(K_WIFI_SSID);
  kv_remove(K_WIFI_PW);
  return true;
}

// ── Server configuration (host + device_id + device_token) ───────────────

bool loadServerConfig(char* host,        size_t hLen,
                      char* deviceId,    size_t dLen,
                      char* deviceToken, size_t tLen) {
  bool a = _getString(K_SRV_HOST,  host,        hLen);
  bool b = _getString(K_SRV_DEVID, deviceId,    dLen);
  bool c = _getString(K_SRV_TOKEN, deviceToken, tLen);
  return a && b && c;
}

bool saveServerConfig(const char* host, const char* deviceId, const char* deviceToken) {
  return _setString(K_SRV_HOST,  host)     &&
         _setString(K_SRV_DEVID, deviceId) &&
         _setString(K_SRV_TOKEN, deviceToken);
}

bool clearServerConfig() {
  kv_remove(K_SRV_HOST);
  kv_remove(K_SRV_DEVID);
  kv_remove(K_SRV_TOKEN);
  return true;
}

}  // namespace storage
