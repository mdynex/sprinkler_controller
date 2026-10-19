#pragma once
#include <kvstore_global_api.h>
#include <string.h>
#include "jobs.h"

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
//   job_0 .. job_7             — one persisted job per slot (see below)
//   last_alive                 — epoch of the last liveness write, used at
//                                boot to work out what was missed while off
//
// The values survive power cycles. Calling clearWifiCreds() or
// clearServerConfig() forces the device back through the relevant
// onboarding step on next boot.
//
// Dependency direction: storage.h includes jobs.h, never the other way round.
// jobs.h calls back into persistence through the onJobPersistNeeded hook.
// =============================================================================

namespace storage {

namespace {

constexpr const char* K_WIFI_SSID    = "wifi_ssid";
constexpr const char* K_WIFI_PW      = "wifi_pw";
constexpr const char* K_SRV_HOST     = "srv_host";
constexpr const char* K_SRV_DEVID    = "srv_devid";
constexpr const char* K_SRV_TOKEN    = "srv_token";
constexpr const char* K_PAIR_SKIPPED = "pair_skipped";   // "1" if user chose standalone mode
constexpr const char* K_LAST_ALIVE   = "last_alive";
constexpr const char* K_JOB_NEXT_ID  = "job_next";   // next job id to hand out

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
  kv_remove(K_PAIR_SKIPPED);   // Re-pair also re-enables the pairing screen on next boot
  return true;
}

// ── Standalone mode flag ──────────────────────────────────────────────────
// True if the user chose Skip on the pairing screen and wants to run the
// controller without a server.

bool loadPairSkipped() {
  char buf[2] = "";
  if (!_getString(K_PAIR_SKIPPED, buf, sizeof(buf))) return false;
  return buf[0] == '1';
}

bool savePairSkipped() {
  return _setString(K_PAIR_SKIPPED, "1");
}

// ── Jobs ───────────────────────────────────────────────────────────────────
//
// One KVStore key per slot ("job_0".."job_7") so rewriting a single job
// doesn't rewrite all of them. Each blob is a fixed header followed by
// exactly stepCount ZoneSteps, so an 8-step job costs ~130 bytes rather than
// the ~590 the full in-RAM struct would take.
//
// The magic + version prefix means a firmware upgrade that changes the layout
// can reject old blobs cleanly instead of loading garbage into a struct that
// drives mains-voltage relays.

namespace {

constexpr uint16_t JOB_BLOB_MAGIC   = 0x4A42;  // 'JB'
constexpr uint8_t  JOB_BLOB_VERSION = 1;

struct JobBlobHeader {
  uint16_t magic;
  uint8_t  version;
  uint8_t  stepCount;
  int32_t  id;
  char     name[32];
  char     origin[8];
  uint8_t  enabled;
  uint8_t  repeat;
  uint8_t  daysMask;
  uint8_t  onComplete;
  int16_t  runHour;
  int16_t  runMinute;
  uint32_t runAtEpoch;
  uint32_t lastRunEpoch;
};

// Largest blob a single slot can hold.
constexpr size_t JOB_BLOB_MAX = sizeof(JobBlobHeader) + JOB_MAX_STEPS * sizeof(ZoneStep);

void _jobKey(int slot, char* out, size_t outLen) {
  snprintf(out, outLen, "job_%d", slot);
}

}  // anonymous namespace

// Persist one job slot. Writing an unused slot removes the key instead.
bool saveJob(int slot, const Job& j) {
  if (slot < 0 || slot >= MAX_JOBS) return false;
  char key[12];
  _jobKey(slot, key, sizeof(key));

  // Transient jobs (a timed zone run from POST /command) are deliberately not
  // durable — clear any stale key rather than writing them.
  if (!j.used || j.isTransient) { kv_remove(key); return true; }

  uint8_t blob[JOB_BLOB_MAX];
  JobBlobHeader h = {};
  h.magic        = JOB_BLOB_MAGIC;
  h.version      = JOB_BLOB_VERSION;
  h.stepCount    = (uint8_t)j.stepCount;
  h.id           = j.id;
  h.enabled      = j.enabled ? 1 : 0;
  h.repeat       = j.repeat;
  h.daysMask     = j.daysMask;
  h.onComplete   = j.onComplete;
  h.runHour      = (int16_t)j.runHour;
  h.runMinute    = (int16_t)j.runMinute;
  h.runAtEpoch   = j.runAtEpoch;
  h.lastRunEpoch = j.lastRunEpoch;
  memcpy(h.name,   j.name,   sizeof(h.name));
  memcpy(h.origin, j.origin, sizeof(h.origin));

  memcpy(blob, &h, sizeof(h));
  size_t stepBytes = (size_t)j.stepCount * sizeof(ZoneStep);
  memcpy(blob + sizeof(h), j.steps, stepBytes);

  return kv_set(key, blob, sizeof(h) + stepBytes, 0) == 0;
}

// Load one job slot into `j`. Returns false if the slot is empty or the blob
// is unreadable / from an incompatible firmware version.
bool loadJob(int slot, Job& j) {
  if (slot < 0 || slot >= MAX_JOBS) return false;
  char key[12];
  _jobKey(slot, key, sizeof(key));

  uint8_t blob[JOB_BLOB_MAX];
  size_t actual = 0;
  if (kv_get(key, blob, sizeof(blob), &actual) != 0) return false;
  if (actual < sizeof(JobBlobHeader)) return false;

  JobBlobHeader h;
  memcpy(&h, blob, sizeof(h));
  if (h.magic != JOB_BLOB_MAGIC || h.version != JOB_BLOB_VERSION) return false;
  if (h.stepCount > JOB_MAX_STEPS) return false;
  if (actual != sizeof(h) + (size_t)h.stepCount * sizeof(ZoneStep)) return false;

  jobInitDefaults(j);
  j.used         = true;
  j.id           = h.id;
  j.stepCount    = h.stepCount;
  j.enabled      = h.enabled != 0;
  j.repeat       = h.repeat;
  j.daysMask     = h.daysMask;
  j.onComplete   = h.onComplete;
  j.runHour      = h.runHour;
  j.runMinute    = h.runMinute;
  j.runAtEpoch   = h.runAtEpoch;
  j.lastRunEpoch = h.lastRunEpoch;
  memcpy(j.name,   h.name,   sizeof(j.name));
  memcpy(j.origin, h.origin, sizeof(j.origin));
  j.name[sizeof(j.name) - 1]     = '\0';
  j.origin[sizeof(j.origin) - 1] = '\0';
  memcpy(j.steps, blob + sizeof(h), (size_t)h.stepCount * sizeof(ZoneStep));
  return true;
}

// Write every slot. Called after any mutation that could touch more than one.
//
// Also saves the id counter. Ids must never be reused: the server mirrors
// jobs by device id, so a new job that inherited a deleted job's id after a
// reboot would be matched to the wrong schedule.
void saveAllJobs() {
  for (int i = 0; i < MAX_JOBS; i++) saveJob(i, jobs[i]);
  int32_t next = nextJobId;
  kv_set(K_JOB_NEXT_ID, &next, sizeof(next), 0);
}

// Repopulate `jobs[]` from flash and restore nextJobId: the saved counter,
// or past the highest id seen if that is larger (or nothing was saved yet).
void loadAllJobs() {
  int highest = 0;
  for (int i = 0; i < MAX_JOBS; i++) {
    if (loadJob(i, jobs[i])) {
      if (jobs[i].id > highest) highest = jobs[i].id;
    } else {
      jobInitDefaults(jobs[i]);
    }
  }
  nextJobId = highest + 1;

  int32_t saved = 0;
  size_t  actual = 0;
  if (kv_get(K_JOB_NEXT_ID, &saved, sizeof(saved), &actual) == 0 &&
      actual == sizeof(saved) && saved > nextJobId) {
    nextJobId = saved;
  }
}

// ── Liveness marker ────────────────────────────────────────────────────────
// Written periodically while running so that, at boot, the gap between this
// value and the current time tells us exactly what the device slept through.

bool saveLastAlive(uint32_t epoch) {
  return kv_set(K_LAST_ALIVE, &epoch, sizeof(epoch), 0) == 0;
}

// Returns 0 when no marker has ever been written (first boot).
uint32_t loadLastAlive() {
  uint32_t epoch = 0;
  size_t actual = 0;
  if (kv_get(K_LAST_ALIVE, &epoch, sizeof(epoch), &actual) != 0) return 0;
  if (actual != sizeof(epoch)) return 0;
  return epoch;
}

}  // namespace storage
