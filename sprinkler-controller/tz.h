#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// tz.h — Local time from a POSIX TZ rule, including daylight saving
//
// The controller used to add a fixed UTC_OFFSET_SECONDS, so every schedule
// ran an hour off for the half of the year the clocks are changed. This
// parses the standard POSIX TZ format that encodes the DST rule too:
//
//   "CST6CDT,M3.2.0,M11.1.0"      US Central (2nd Sun Mar → 1st Sun Nov)
//   "EST5EDT,M3.2.0,M11.1.0"      US Eastern
//   "GMT0BST,M3.5.0/1,M10.5.0"    UK
//   "AEST-10AEDT,M10.1.0,M4.1.0/3" Sydney (southern hemisphere)
//   "MST7"                         Arizona (no DST)
//
// Supported: names (letters or <+05>), offsets [+-]hh[:mm[:ss]] (POSIX sign:
// positive = west of UTC), optional DST offset (default std - 1h), and
// Mm.w.d[/time] transition rules (default time 02:00). The rarer Jn / n day
// rules are not supported; tzParse() returns false for them.
//
// No Arduino dependencies, so it can be unit-tested on a PC.
// =============================================================================

struct TzRule {
  uint8_t month;      // 1..12
  uint8_t week;       // 1..5 (5 = last)
  uint8_t weekday;    // 0 = Sunday
  int32_t timeSec;    // local wall-clock time of the change, seconds after midnight
};

struct TzInfo {
  int32_t stdOffset;  // seconds EAST of UTC (local = utc + offset)
  int32_t dstOffset;
  bool    hasDst;
  TzRule  start;      // standard → DST
  TzRule  end;        // DST → standard
};

namespace tz_detail {

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t  era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

inline int32_t yearFromDays(int32_t z) {
  z += 719468;
  const int32_t  era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp  = (5 * doy + 2) / 153;
  const uint32_t m   = mp < 10 ? mp + 3 : mp - 9;
  return (int32_t)yoe + era * 400 + (m <= 2);
}

inline int daysInMonth(int32_t y, int m) {
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dim[m - 1];
}

// Seconds since the epoch, in LOCAL wall-clock terms, of a rule's change in
// year `y` (i.e. midnight of that day + timeSec, not yet converted to UTC).
inline int64_t ruleLocal(const TzRule& r, int32_t y) {
  int32_t first = daysFromCivil(y, r.month, 1);
  int     dow1  = (int)(((first % 7) + 7 + 4) % 7);   // 1970-01-01 was a Thursday
  int     day   = 1 + ((r.weekday - dow1 + 7) % 7) + (r.week - 1) * 7;
  while (day > daysInMonth(y, r.month)) day -= 7;     // week 5 = "last"
  return (int64_t)(first + day - 1) * 86400 + r.timeSec;
}

// Parse [+-]hh[:mm[:ss]] into seconds. Advances *p. Returns false on junk.
inline bool parseTime(const char** p, int32_t* out) {
  const char* s = *p;
  int sign = 1;
  if (*s == '+') s++;
  else if (*s == '-') { sign = -1; s++; }
  if (*s < '0' || *s > '9') return false;
  int32_t v = 0, part = 0;
  int field = 0;
  while (true) {
    part = 0;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') part = part * 10 + (*s++ - '0');
    v += part * (field == 0 ? 3600 : field == 1 ? 60 : 1);
    if (*s == ':' && field < 2) { s++; field++; continue; }
    break;
  }
  *out = sign * v;
  *p = s;
  return true;
}

inline bool parseName(const char** p) {
  const char* s = *p;
  if (*s == '<') {
    while (*s && *s != '>') s++;
    if (*s != '>') return false;
    s++;
  } else {
    const char* b = s;
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) s++;
    if (s - b < 3) return false;
  }
  *p = s;
  return true;
}

inline bool parseRule(const char** p, TzRule* r) {
  const char* s = *p;
  if (*s != 'M') return false;      // Jn / n day formats not supported
  s++;
  int vals[3];
  for (int i = 0; i < 3; i++) {
    if (*s < '0' || *s > '9') return false;
    vals[i] = (int)strtol(s, (char**)&s, 10);
    if (i < 2) { if (*s != '.') return false; s++; }
  }
  if (vals[0] < 1 || vals[0] > 12 || vals[1] < 1 || vals[1] > 5 ||
      vals[2] < 0 || vals[2] > 6) return false;
  r->month   = (uint8_t)vals[0];
  r->week    = (uint8_t)vals[1];
  r->weekday = (uint8_t)vals[2];
  r->timeSec = 2 * 3600;
  if (*s == '/') {
    s++;
    if (!parseTime(&s, &r->timeSec)) return false;
  }
  *p = s;
  return true;
}

}  // namespace tz_detail

// Parse a POSIX TZ string. Returns false (leaving `out` untouched) if it
// isn't one this parser understands.
inline bool tzParse(const char* spec, TzInfo* out) {
  using namespace tz_detail;
  if (!spec || !out) return false;
  TzInfo t = {};
  const char* s = spec;
  int32_t posix = 0;
  if (!parseName(&s) || !parseTime(&s, &posix)) return false;
  t.stdOffset = -posix;                 // POSIX counts west as positive
  if (*s == '\0') { t.hasDst = false; *out = t; return true; }

  if (!parseName(&s)) return false;
  t.hasDst    = true;
  t.dstOffset = t.stdOffset + 3600;
  if (*s && *s != ',') {
    if (!parseTime(&s, &posix)) return false;
    t.dstOffset = -posix;
  }
  if (*s != ',') return false;          // DST with no rule: unsupported
  s++;
  if (!parseRule(&s, &t.start)) return false;
  if (*s != ',') return false;
  s++;
  if (!parseRule(&s, &t.end)) return false;
  if (*s != '\0') return false;
  *out = t;
  return true;
}

// Offset (seconds east of UTC) in effect at `utc`.
inline int32_t tzOffsetAt(const TzInfo& t, int64_t utc) {
  using namespace tz_detail;
  if (!t.hasDst) return t.stdOffset;
  int32_t y = yearFromDays((int32_t)((utc + t.stdOffset) / 86400));
  // A start rule is written in standard time, an end rule in DST.
  int64_t startUtc = ruleLocal(t.start, y) - t.stdOffset;
  int64_t endUtc   = ruleLocal(t.end,   y) - t.dstOffset;
  bool inDst = (startUtc < endUtc)
             ? (utc >= startUtc && utc < endUtc)     // northern hemisphere
             : (utc >= startUtc || utc < endUtc);    // southern: wraps the year
  return inDst ? t.dstOffset : t.stdOffset;
}
