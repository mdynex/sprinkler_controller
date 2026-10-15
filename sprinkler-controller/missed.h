#pragma once
#include "jobs.h"
#include "storage.h"
#include "ntp.h"

// =============================================================================
// missed.h — "What did I sleep through?" detection
//
// Jobs persist across reboots, which raises a question the old single-schedule
// firmware never had to answer: if the board was off at 06:00, what should
// happen to the 06:00 job?
//
// The device writes a `last_alive` epoch to flash every few minutes. At boot,
// the gap between that marker and the current time is exactly the window the
// device was absent for, so any fire time inside it was missed.
//
// What happens then depends on how late we are and what kind of job it is:
//
//   within GRACE_SECONDS   → run it late, nothing was really lost. This is the
//                            watchdog-reset case: a board that resets at 06:00
//                            and is back by 06:02 must not silently drop that
//                            day's watering.
//
//   beyond GRACE_SECONDS   → report it to the server, and then:
//                              one-shot  → delete it (a run command for a time
//                                          that has passed is stale)
//                              recurring → leave it armed; it fires again next
//                                          time round. A power cut must never
//                                          silently wipe a weekly schedule.
//
// Reports are buffered in RAM and ride out on the next heartbeat (see
// heartbeat.h), which is what turns them into a notification in the app.
// =============================================================================

namespace missed {

// How late a job may be and still just run instead of counting as missed.
constexpr uint32_t GRACE_SECONDS = 30 * 60;

// How often the liveness marker is rewritten. Every 5 minutes keeps flash
// wear modest (~105k writes/year, well inside KVStore's wear levelling) while
// bounding how much of an outage we can misjudge.
constexpr unsigned long ALIVE_WRITE_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Don't walk more than this many days of recurring fire times, so a device
// that was unplugged for a year doesn't spend the whole boot in a loop.
constexpr uint32_t MAX_SCAN_DAYS = 60;

constexpr int MAX_MISSED = 8;

struct MissedRecord {
  int      jobId;
  char     name[32];
  uint32_t scheduledEpoch;
  bool     deleted;       // true when the job was removed (stale one-shot)
};

MissedRecord records[MAX_MISSED];
int          recordCount = 0;

bool _scanned = false;   // the boot scan runs exactly once

// ── Reporting ──────────────────────────────────────────────────────────────

int count() { return recordCount; }

void clear() { recordCount = 0; }

static void _record(const Job& j, uint32_t scheduledEpoch, bool deleted) {
  if (recordCount >= MAX_MISSED) return;   // oldest wins; a flood is still one alert
  MissedRecord& r = records[recordCount++];
  r.jobId          = j.id;
  r.scheduledEpoch = scheduledEpoch;
  r.deleted        = deleted;
  strncpy(r.name, j.name, sizeof(r.name) - 1);
  r.name[sizeof(r.name) - 1] = '\0';
}

// Write the buffered records as a JSON array into `buf`.
// Returns chars written (excluding the null), or 0 if it didn't fit.
int toJson(char* buf, size_t bufLen) {
  int n = snprintf(buf, bufLen, "[");
  if (n < 0 || (size_t)n >= bufLen) return 0;
  for (int i = 0; i < recordCount; i++) {
    int w = snprintf(buf + n, bufLen - n,
      "%s{\"job_id\":%d,\"name\":\"%s\",\"scheduled_at\":%lu,\"deleted\":%s}",
      i ? "," : "",
      records[i].jobId, records[i].name,
      (unsigned long)records[i].scheduledEpoch,
      records[i].deleted ? "true" : "false");
    if (w < 0 || (size_t)(n + w) >= bufLen) return 0;
    n += w;
  }
  int w = snprintf(buf + n, bufLen - n, "]");
  if (w < 0 || (size_t)(n + w) >= bufLen) return 0;
  return n + w;
}

// ── Liveness marker ────────────────────────────────────────────────────────

// Call from loop(). Rewrites the marker on its interval once time is known.
void tick() {
  if (!ntpReady) return;
  static unsigned long last = 0;
  if (last != 0 && millis() - last < ALIVE_WRITE_INTERVAL_MS) return;
  last = millis();
  storage::saveLastAlive((uint32_t)ntpEpoch());
}

// ── Boot scan ──────────────────────────────────────────────────────────────

// Local-midnight epoch of the day containing `epoch`.
static uint32_t _dayStart(uint32_t epoch) {
  return (epoch / 86400UL) * 86400UL;
}

// Day-of-week (0=Sun) for a local-midnight epoch, matching ntpDayOfWeek().
static int _dowOf(uint32_t dayStart) {
  return (int)(((dayStart / 86400UL) + 4UL) % 7UL);
}

// Handle one fire time that fell inside the outage window.
// Returns true if the job was deleted.
static bool _handleMissed(Job& j, uint32_t fireEpoch, uint32_t now, bool& ranLate) {
  if (now - fireEpoch <= GRACE_SECONDS) {
    // Close enough to just do it.
    //
    // One-shots need no help: jobSchedulerCheck() fires them as soon as
    // now >= runAtEpoch, which is already true. Recurring jobs only match on
    // an exact hour:minute, so that window has gone — start those explicitly.
    if (j.repeat != REPEAT_NONE && !runState.running && !ranLate) {
      Serial.print("[missed] Running late: "); Serial.println(j.name);
      if (startJob(j)) {
        j.lastRunEpoch = now;
        ranLate = true;
      }
    }
    return false;
  }

  bool deleted = (j.repeat == REPEAT_NONE);
  Serial.print("[missed] "); Serial.print(j.name);
  Serial.println(deleted ? " missed - deleting (one-shot)" : " missed - still armed");
  _record(j, fireEpoch, deleted);
  if (deleted) jobInitDefaults(j);
  return deleted;
}

// Walk every job's fire times across the outage window.
// Safe to call repeatedly — it only does work once, and only once NTP is up.
void scanAtBoot() {
  if (_scanned) return;
  if (!ntpReady) return;      // never evaluate against an unset clock
  _scanned = true;

  uint32_t now       = (uint32_t)ntpEpoch();
  uint32_t lastAlive = storage::loadLastAlive();

  // First boot, or the clock moved backwards (NTP correction, DST, a bad
  // marker). Either way there's no trustworthy window — just re-anchor.
  if (lastAlive == 0 || now <= lastAlive) {
    storage::saveLastAlive(now);
    return;
  }

  Serial.print("[missed] Outage window: ");
  Serial.print(now - lastAlive);
  Serial.println(" seconds");

  bool ranLate = false;
  bool dirty   = false;

  for (int i = 0; i < MAX_JOBS; i++) {
    Job& j = jobs[i];
    if (!j.used || !j.enabled) continue;

    if (j.repeat == REPEAT_NONE) {
      if (j.runAtEpoch > lastAlive && j.runAtEpoch <= now) {
        if (_handleMissed(j, j.runAtEpoch, now, ranLate)) dirty = true;
      }
      continue;
    }

    // Recurring: find the most recent scheduled time inside the outage.
    //
    // Walking backwards matters twice over. It reports one entry per job
    // rather than one per missed occurrence — a week-long outage of a
    // Mon/Wed/Fri schedule is one notification, not three, and can't flood the
    // record buffer. And the newest occurrence is the only one that could
    // still be inside the grace window, so it's the only one worth running.
    uint32_t firstDay = _dayStart(lastAlive);
    uint32_t lastDay  = _dayStart(now);
    if ((lastDay - firstDay) / 86400UL > MAX_SCAN_DAYS) {
      firstDay = lastDay - MAX_SCAN_DAYS * 86400UL;
    }
    for (uint32_t day = lastDay; ; day -= 86400UL) {
      if (((j.daysMask >> _dowOf(day)) & 1) != 0) {
        uint32_t fire = day + (uint32_t)j.runHour * 3600UL
                            + (uint32_t)j.runMinute * 60UL;
        if (fire > lastAlive && fire <= now) {
          if (_handleMissed(j, fire, now, ranLate)) dirty = true;
          break;   // one report per job
        }
      }
      if (day <= firstDay) break;   // guard: unsigned, can't test day >= 0
    }
  }

  if (dirty) storage::saveAllJobs();
  storage::saveLastAlive(now);

  if (recordCount > 0) {
    Serial.print("[missed] "); Serial.print(recordCount);
    Serial.println(" job(s) missed - will report on next heartbeat");
  }
}

}  // namespace missed
