#pragma once
#include <Arduino_GigaDisplay_GFX.h>
#include <Arduino_GigaDisplayTouch.h>
#include <Arduino_GigaDisplay.h>
#include "zones.h"
#include "jobs.h"
#include "storage.h"
#include "ntp.h"

// ── Colors (RGB565) ────────────────────────────────────────────────────────
#define C_BG        0x1082   // dark blue-gray background
#define C_BLACK     0x0000
#define C_WHITE     0xFFFF
#define C_DIM       0x7BEF   // light gray (secondary text)
#define C_GREEN     0x07E0
#define C_RED       0xF800
#define C_BLUE      0x001F
#define C_ORANGE    0xFD20
#define C_GRAY      0x4208
#define C_DARKGRAY  0x2104
#define C_ZONE_OFF  0x2945   // slate blue (inactive zone)
#define C_ROW_A     0x2104   // edit row alternating colors
#define C_ROW_B     0x18A4

// ── Screen layout (800 × 480) ──────────────────────────────────────────────
#define SCR_W    800
#define SCR_H    480
#define HDR_H     50   // header bar
#define STS_H     40   // status bar (bottom)
#define NAV_H     70   // nav/button bar (above status)
#define NAV_Y    370   // SCR_H - NAV_H - STS_H
#define STS_Y    440   // SCR_H - STS_H

// Home: zone grid (4 cols × 2 rows)
#define ZN_COLS    4
#define ZN_PAD    15
#define ZN_W     181   // (800 - 5*15) / 4
#define ZN_H     137   // (320 - 3*15) / 2  (CONT_H=320)


// ── Global objects ─────────────────────────────────────────────────────────
GigaDisplay_GFX          gfx;
Arduino_GigaDisplayTouch touch;
GigaDisplayBacklight     _backlight;

// ── Screen state ───────────────────────────────────────────────────────────
enum Screen {
  SCR_HOME,
  SCR_SCHEDULES,
  SCR_EDIT,
  SCR_ZONE_SETTINGS,
  SCR_NEW_SCHEDULE,       // Create-on-device schedule builder (step 1: time/zones/duration)
  SCR_NEW_DAYS,           // Create-on-device schedule builder (step 2: days of week)
  SCR_PAIRING,            // STATE_PAIRING — QR + claim code
  SCR_WIFI_LIST,          // STATE_WIFI_SETUP — pick a network
  SCR_WIFI_PASSWORD,      // STATE_WIFI_SETUP — keyboard for the password
  SCR_WIFI_CONNECTING,    // STATE_WIFI_SETUP — "Connecting…" splash
  SCR_SETTINGS,           // gear icon → Settings menu
  SCR_ABOUT               // Settings → About
};
Screen        currentScreen   = SCR_HOME;

// Forward declarations — definitions live in pairing.h, wifi_setup.h, settings.h
// (all included after display.h in the .ino so the compiler can resolve them)
void _drawPairingScreen();
void _handlePairingTouch(int tx, int ty);
namespace wifi_setup {
  void drawListScreen();
  void drawPasswordScreen();
  void drawConnectingScreen();
  void handleListTouch(int tx, int ty);
  void handlePasswordTouch(int tx, int ty);
}
namespace settings {
  void drawSettingsScreen();
  void drawAboutScreen();
  void handleSettingsTouch(int tx, int ty);
  void handleAboutTouch(int tx, int ty);
}
bool          needsRedraw     = true;
String        _lastTime       = "";
bool          _lastRunning    = false;
bool          _displayAsleep  = false;
bool          _pendingWake    = false;
unsigned long _lastActivityMs = 0;

// Touch event — set by callback, consumed in displayLoop()
volatile bool _touched     = false;
volatile int  _touchX      = 0;
volatile int  _touchY      = 0;
volatile bool _fingerDown  = false;
volatile bool _contactSeen = false;  // pulsed true by callback; timed in displayLoop

// Keep the callback side-effect-free of millis() — mbed interrupt context
// makes millis() unreliable. Only set simple boolean flags here; all timing
// happens in displayLoop() where millis() is safe.
void _onTouch(uint8_t contacts, GDTpoint_t* pts) {
  if (contacts > 0) {
    _contactSeen = true;
    if (!_fingerDown) {
      _touchX     = SCR_W - pts[0].y;   // flipped landscape X
      _touchY     = pts[0].x;            // flipped landscape Y
      _touched    = true;
      _fingerDown = true;
    }
  } else {
    _fingerDown = false;   // explicit lift when the IC does report contacts==0
  }
}

// ── Edit screen state ──────────────────────────────────────────────────────
uint8_t _editDaysMask = 0;   // bit 0=Sun ... 6=Sat. 0 = run once (no repeat).
int     _editHour     = 6;
int     _editMin      = 0;
int     _editStepsPage = 0;  // 0-indexed page of the zone step list

// ── New-job builder state ──────────────────────────────────────────────────
bool    _newZones[ZONE_COUNT] = {false};
int     _newDuration          = 10;  // minutes, 1..120
int     _newHour              = 6;
int     _newMin               = 0;
uint8_t _newDaysMask          = 0x7F;  // default to all days

// ── Zone Settings paging ───────────────────────────────────────────────────
#define ZS_ZONES_PER_PAGE  4
int _zoneSettingsPage = 0;   // 0 = zones 1-4, 1 = zones 5-8, etc.

// ── Job list state ─────────────────────────────────────────────────────────
#define SL_ROWS_PER_PAGE  4
int _schedPage        = 0;   // 0-indexed page of the job list
int _selectedJobIndex = -1;  // slot in jobs[] the EDIT screen is working on

// Collect the slot indices of every used job, in slot order.
// Returns how many were written to `out`.
int _usedJobIndices(int* out, int maxOut) {
  int n = 0;
  for (int i = 0; i < MAX_JOBS && n < maxOut; i++) {
    if (jobs[i].used) out[n++] = i;
  }
  return n;
}

// The job the EDIT screen is editing, or nullptr if it went away underneath
// us (deleted by the server, or retired by its own on_complete policy).
Job* _selectedJob() {
  if (_selectedJobIndex < 0 || _selectedJobIndex >= MAX_JOBS) return nullptr;
  if (!jobs[_selectedJobIndex].used) return nullptr;
  return &jobs[_selectedJobIndex];
}

// Load the selected job's repeat settings into edit state.
// Zone steps are managed by the app and are not editable on the display.
void _loadEditState() {
  Job* j = _selectedJob();
  if (!j) return;
  _editDaysMask  = (j->repeat == REPEAT_NONE) ? 0 : j->daysMask;
  _editHour      = j->runHour;
  _editMin       = j->runMinute;
  _editStepsPage = 0;
}

// Save the repeat settings back to the selected job. Zone steps are untouched.
//
// Clearing every day turns the job back into a one-shot; setting any day
// promotes it to a schedule. That is the whole "run command becomes a
// schedule" gesture, and it also has to fix up on_complete — a promoted run
// command that kept on_complete=delete would vanish after its first run.
void _saveEditState() {
  Job* j = _selectedJob();
  if (!j) return;
  uint8_t wasRepeat = j->repeat;

  j->daysMask  = _editDaysMask;
  j->runHour   = _editHour;
  j->runMinute = _editMin;

  if (_editDaysMask == 0)          j->repeat = REPEAT_NONE;
  else if (_editDaysMask == 0x7F)  j->repeat = REPEAT_DAILY;
  else                             j->repeat = REPEAT_WEEKLY;

  if (j->repeat != wasRepeat) {
    j->onComplete = (j->repeat == REPEAT_NONE) ? ON_COMPLETE_DELETE
                                               : ON_COMPLETE_KEEP;
    // A job edited into a one-shot needs a concrete time to fire at, or the
    // scheduler would never pick it up.
    if (j->repeat == REPEAT_NONE && ntpReady) {
      uint32_t today = (ntpEpoch() / 86400UL) * 86400UL;
      uint32_t fire  = today + (uint32_t)_editHour * 3600UL
                             + (uint32_t)_editMin * 60UL;
      if (fire <= (uint32_t)ntpEpoch()) fire += 86400UL;   // already passed → tomorrow
      j->runAtEpoch = fire;
    }
  }
  storage::saveAllJobs();
}

// ── Sleep / wake ───────────────────────────────────────────────────────────

// Safe to call from anywhere (including WiFi request handlers).
// Actual backlight/redraw happens in displayLoop() to avoid hardware conflicts.
void wakeDisplay() {
  _lastActivityMs = millis();
  if (_displayAsleep) _pendingWake = true;
}

void _sleepDisplay() {
  _backlight.set(0);
  _displayAsleep = true;
}

// ── Drawing utilities ──────────────────────────────────────────────────────

bool _inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

// Draws a rounded button with centered label. sz = GFX text size (1 or 2).
void _btn(int x, int y, int w, int h, uint16_t color, const char* lbl, int sz = 2, uint16_t textColor = C_WHITE) {
  gfx.fillRoundRect(x, y, w, h, 6, color);
  gfx.setTextColor(textColor);
  gfx.setTextSize(sz);
  int tw = strlen(lbl) * sz * 6;
  int th = sz * 8;
  gfx.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  gfx.print(lbl);
}

// ── Common elements ────────────────────────────────────────────────────────

// Hit-test region for the home-screen "Settings" icon in the header.
// The clock occupies x = ~725..785 (5 chars × 12 px at size 2, anchored to
// the right edge with a 15 px margin), so place the menu icon clear of it.
constexpr int HDR_MENU_X = 670;   // x of menu icon (3 bars). Right edge = 702.
constexpr int HDR_MENU_Y = 14;
constexpr int HDR_MENU_W = 32;
constexpr int HDR_MENU_H = 22;

void _drawHeader(const char* title) {
  gfx.fillRect(0, 0, SCR_W, HDR_H, C_BLACK);
  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(2);
  gfx.setCursor(15, 17);
  gfx.print(title);

  // Settings/menu icon (three horizontal bars) — only shown on the home screen
  if (currentScreen == SCR_HOME) {
    for (int i = 0; i < 3; i++) {
      gfx.fillRect(HDR_MENU_X, HDR_MENU_Y + i * 8, HDR_MENU_W, 3, C_WHITE);
    }
  }

  if (ntpReady) {
    String t = ntpFormattedTime().substring(0, 5);  // show HH:MM only
    gfx.setCursor(SCR_W - 15 - (int)t.length() * 12, 17);
    gfx.print(t);
  }
}

// Redraws only the clock region in the header — avoids a full screen redraw
// when the minute ticks over.
void _updateClockDisplay() {
  // Erase the right side of the header where the clock lives
  gfx.fillRect(SCR_W - 90, 0, 90, HDR_H, C_BLACK);
  if (ntpReady) {
    String t = ntpFormattedTime().substring(0, 5);
    gfx.setTextColor(C_WHITE);
    gfx.setTextSize(2);
    gfx.setCursor(SCR_W - 15 - (int)t.length() * 12, 17);
    gfx.print(t);
  }
}

void _drawStatusBar() {
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
  gfx.setTextColor(C_DIM);
  gfx.setTextSize(1);
  gfx.setCursor(10, STS_Y + 14);
  Job* running = runState.running ? findJob(runState.jobId) : nullptr;
  if (running && runState.stepIndex < running->stepCount) {
    ZoneStep& step = running->steps[runState.stepIndex];
    unsigned long elapsed   = (millis() - runState.stepStart) / 1000UL;
    unsigned long remaining = step.duration > (int)elapsed ? step.duration - elapsed : 0;
    gfx.print("Running: ");
    gfx.print(running->name);
    if (step.zoneId == 0) {
      gfx.print("  |  Soaking");
    } else {
      gfx.print("  |  Zone ");
      gfx.print(step.zoneId);
    }
    gfx.print("  |  Step ");
    gfx.print(runState.stepIndex + 1);
    gfx.print("/");
    gfx.print(running->stepCount);
    gfx.print("  |  ");
    gfx.print(remaining);
    gfx.print("s remaining");
  } else {
    gfx.print("Idle");
  }
}

// ── HOME screen ────────────────────────────────────────────────────────────

void _drawZoneBtn(int idx) {
  int col  = idx % ZN_COLS;
  int row  = idx / ZN_COLS;
  int x    = ZN_PAD + col * (ZN_W + ZN_PAD);
  int y    = HDR_H  + ZN_PAD + row * (ZN_H + ZN_PAD);

  bool enabled = zoneEnabled[idx];
  uint16_t bg  = !enabled ? C_DARKGRAY : (zoneState[idx] ? C_GREEN : C_ZONE_OFF);
  gfx.fillRoundRect(x, y, ZN_W, ZN_H, 10, bg);
  gfx.drawRoundRect(x, y, ZN_W, ZN_H, 10, enabled ? C_WHITE : C_GRAY);

  gfx.setTextColor(enabled ? C_WHITE : C_DIM);
  gfx.setTextSize(3);
  char top[8];
  snprintf(top, sizeof(top), "Zone %d", idx + 1);
  int tw = strlen(top) * 18;
  gfx.setCursor(x + (ZN_W - tw) / 2, y + ZN_H / 2 - 24);
  gfx.print(top);

  gfx.setTextSize(2);
  const char* st = !enabled ? "OFF" : (zoneState[idx] ? "ON" : "OFF");
  tw = strlen(st) * 12;
  gfx.setCursor(x + (ZN_W - tw) / 2, y + ZN_H / 2 + 8);
  gfx.print(st);

  // Watering rate (hidden when disabled)
  if (enabled) {
    char rate[12];
    formatRate(zoneRate[idx], rate, sizeof(rate));
    strncat(rate, " in/hr", sizeof(rate) - strlen(rate) - 1);
    gfx.setTextSize(1);
    gfx.setTextColor(C_DIM);
    tw = strlen(rate) * 6;
    gfx.setCursor(x + (ZN_W - tw) / 2, y + ZN_H - 18);
    gfx.print(rate);
  }
}

void _drawHomeScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("SPRINKLER CONTROLLER");
  for (int i = 0; i < ZONE_COUNT; i++) _drawZoneBtn(i);
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn( 20,  NAV_Y + 10, 220, 50, C_GRAY, "SCHEDULES");
  _btn(290,  NAV_Y + 10, 220, 50, C_GRAY, "ZONES");
  _btn(560,  NAV_Y + 10, 220, 50, C_RED,  "STOP ALL");
  _drawStatusBar();
}

void _handleHomeTouch(int tx, int ty) {
  // Settings menu — tap on the hamburger icon in the header
  if (_inRect(tx, ty, HDR_MENU_X - 4, HDR_MENU_Y - 4, HDR_MENU_W + 8, HDR_MENU_H + 8)) {
    currentScreen = SCR_SETTINGS;
    needsRedraw = true;
    return;
  }
  for (int i = 0; i < ZONE_COUNT; i++) {
    int x = ZN_PAD + (i % ZN_COLS) * (ZN_W + ZN_PAD);
    int y = HDR_H  + ZN_PAD + (i / ZN_COLS) * (ZN_H + ZN_PAD);
    if (_inRect(tx, ty, x, y, ZN_W, ZN_H)) {
      if (zoneEnabled[i]) {
        setZone(i, !zoneState[i]);
        _drawZoneBtn(i);
        _drawStatusBar();
      }
      return;
    }
  }
  if (_inRect(tx, ty,  20, NAV_Y + 10, 220, 50)) { currentScreen = SCR_SCHEDULES;    needsRedraw = true; return; }
  if (_inRect(tx, ty, 290, NAV_Y + 10, 220, 50)) { currentScreen = SCR_ZONE_SETTINGS; needsRedraw = true; return; }
  if (_inRect(tx, ty, 560, NAV_Y + 10, 220, 50)) {
    stopJob();
    for (int i = 0; i < ZONE_COUNT; i++) _drawZoneBtn(i);
    _drawStatusBar();
  }
}

// ── Day-toggle row primitives (used by SCHEDULES, EDIT, NEW SCHEDULE) ──────
constexpr int DAY_W   = 80;
constexpr int DAY_H   = 35;
constexpr int DAY_GAP = 5;
constexpr int DAY_X0  = 20;

void _drawDaysRow(int yBase, uint8_t mask) {
  const char* labels[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  for (int d = 0; d < 7; d++) {
    int x = DAY_X0 + d * (DAY_W + DAY_GAP);
    bool on = (mask >> d) & 1;
    _btn(x, yBase, DAY_W, DAY_H,
         on ? C_GREEN : C_DARKGRAY,
         labels[d], 1,
         on ? C_BLACK : C_WHITE);
  }
}

// If (tx,ty) falls on a day toggle within `yBase`, return the day index 0..6;
// otherwise return -1.
int _hitDay(int tx, int ty, int yBase) {
  if (ty < yBase || ty >= yBase + DAY_H) return -1;
  for (int d = 0; d < 7; d++) {
    int x = DAY_X0 + d * (DAY_W + DAY_GAP);
    if (tx >= x && tx <= x + DAY_W) return d;
  }
  return -1;
}

// Format a daysMask as a friendly summary string ("Daily", "Weekdays", or
// individual initials joined by spaces).
void _formatDaysMask(uint8_t mask, char* out, size_t outLen) {
  if (outLen == 0) return;
  if (mask == 0)    { strncpy(out, "Manual only", outLen - 1); out[outLen-1]=0; return; }
  if (mask == 0x7F) { strncpy(out, "Every day",   outLen - 1); out[outLen-1]=0; return; }
  if (mask == 0x3E) { strncpy(out, "Weekdays",    outLen - 1); out[outLen-1]=0; return; }
  if (mask == 0x41) { strncpy(out, "Weekends",    outLen - 1); out[outLen-1]=0; return; }
  const char* initials[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
  out[0] = '\0';
  for (int d = 0; d < 7; d++) {
    if ((mask >> d) & 1) {
      if (out[0] != '\0' && strlen(out) + 2 < outLen) strcat(out, " ");
      if (strlen(out) + 3 < outLen) strcat(out, initials[d]);
    }
  }
}

// ── JOBS screen ────────────────────────────────────────────────────────────
//
// One list holds both kinds of entry. A row is a "run command" when the job
// doesn't repeat and a "schedule" when it does — same list, same controls.
// Every row can be toggled off (ON/OFF) and opened for edit/delete.

#define SL_ROW_H    55
#define SL_ROW_GAP   8
#define SL_LIST_Y   (HDR_H + 5)
#define SL_RUN_X    (SCR_W - 140)
#define SL_TOG_X    (SCR_W - 250)
#define SL_TOG_W    100

// Describe when a job next runs, e.g. "MoWeFr 06:00" or "Once 06:00".
static void _formatJobWhen(const Job& j, char* out, size_t outLen) {
  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%02d:%02d", j.runHour, j.runMinute);
  if (j.repeat == REPEAT_NONE) {
    snprintf(out, outLen, "Once  %s", tbuf);
  } else if (j.repeat == REPEAT_DAILY) {
    snprintf(out, outLen, "Daily  %s", tbuf);
  } else {
    char days[32];
    _formatDaysMask(j.daysMask, days, sizeof(days));
    snprintf(out, outLen, "%s  %s", days, tbuf);
  }
}

// Draw the job at jobs[slot] into the given on-screen row.
void _drawSchedRow(int screenRow, int slot) {
  const Job& j = jobs[slot];
  int  y      = SL_LIST_Y + screenRow * (SL_ROW_H + SL_ROW_GAP);
  bool active = jobIsRunning(j);

  uint16_t bg = active     ? 0x0343        // green-ish while watering
              : j.enabled  ? C_DARKGRAY
                           : C_GRAY;       // dimmed when switched off
  gfx.fillRoundRect(10, y, SCR_W - 20, SL_ROW_H, 6, bg);

  gfx.setTextColor(j.enabled ? C_WHITE : C_DIM);
  gfx.setTextSize(2);
  gfx.setCursor(20, y + 10);
  gfx.print(j.name);

  gfx.setTextSize(1);
  gfx.setTextColor(j.repeat != REPEAT_NONE ? C_ORANGE : C_DIM);
  gfx.setCursor(20, y + 36);
  char when[48];
  _formatJobWhen(j, when, sizeof(when));
  gfx.print(when);

  // One-shots that clean up after themselves say so, so a row that is about
  // to disappear doesn't look like a bug.
  if (j.onComplete == ON_COMPLETE_DELETE) {
    gfx.setTextColor(C_DIM);
    gfx.print("  (deletes after run)");
  } else if (j.onComplete == ON_COMPLETE_DISABLE) {
    gfx.setTextColor(C_DIM);
    gfx.print("  (turns off after run)");
  }

  gfx.setTextColor(C_DIM);
  gfx.setCursor(340, y + 22);
  gfx.print(j.stepCount);
  gfx.print(" steps");

  _btn(SL_TOG_X, y + 9, SL_TOG_W, SL_ROW_H - 18,
       j.enabled ? C_BLUE : C_GRAY,
       j.enabled ? "ON" : "OFF", 2);

  _btn(SL_RUN_X, y + 9, 120, SL_ROW_H - 18,
       active ? C_RED : C_GREEN,
       active ? "STOP" : "RUN", 2,
       active ? C_WHITE : C_BLACK);
}

void _drawSchedulesScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("JOBS");

  int idx[MAX_JOBS];
  int total = _usedJobIndices(idx, MAX_JOBS);
  int pages = total == 0 ? 1 : (total + SL_ROWS_PER_PAGE - 1) / SL_ROWS_PER_PAGE;
  if (_schedPage >= pages) _schedPage = pages - 1;
  if (_schedPage < 0)      _schedPage = 0;

  if (total == 0) {
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(2);
    gfx.setCursor(20, HDR_H + 40);
    gfx.print("No jobs yet.");
    gfx.setTextSize(1);
    gfx.setCursor(20, HDR_H + 70);
    gfx.print("Create one on this device, or send one from the app.");

    _btn(SCR_W / 2 - 175, HDR_H + 110, 350, 70, C_BLUE, "+ CREATE NEW JOB", 2);
  } else {
    int start = _schedPage * SL_ROWS_PER_PAGE;
    for (int r = 0; r < SL_ROWS_PER_PAGE && start + r < total; r++) {
      _drawSchedRow(r, idx[start + r]);
    }
  }

  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20, NAV_Y + 10, 160, 50, C_GRAY, "BACK");

  if (total > 0) {
    // "+ NEW" is only reachable here once at least one job exists; the empty
    // state has its own big button.
    _btn(200, NAV_Y + 10, 130, 50, C_BLUE, "+ NEW", 2);
  }
  if (pages > 1) {
    _btn(350, NAV_Y + 10, 60, 50, C_GRAY, "<", 2);
    _btn(420, NAV_Y + 10, 60, 50, C_GRAY, ">", 2);
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(1);
    gfx.setCursor(350, NAV_Y + 2);
    gfx.print("Page ");
    gfx.print(_schedPage + 1);
    gfx.print("/");
    gfx.print(pages);
  }
  _btn(SCR_W - 250, NAV_Y + 10, 230, 50, C_RED, "STOP ALL");
  _drawStatusBar();
}

// Reset the on-device builder to defaults and open it.
static void _openNewJobBuilder() {
  for (int i = 0; i < ZONE_COUNT; i++) _newZones[i] = false;
  _newDuration = 10;
  _newHour     = 6;
  _newMin      = 0;
  _newDaysMask = 0x7F;   // default to every day (user can refine on step 2)
  currentScreen = SCR_NEW_SCHEDULE;
  needsRedraw = true;
}

void _handleSchedulesTouch(int tx, int ty) {
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) { currentScreen = SCR_HOME; needsRedraw = true; return; }

  // STOP ALL
  if (_inRect(tx, ty, SCR_W - 250, NAV_Y + 10, 230, 50)) {
    stopJob();
    needsRedraw = true;
    return;
  }

  int idx[MAX_JOBS];
  int total = _usedJobIndices(idx, MAX_JOBS);
  int pages = total == 0 ? 1 : (total + SL_ROWS_PER_PAGE - 1) / SL_ROWS_PER_PAGE;

  if (total == 0) {
    if (_inRect(tx, ty, SCR_W / 2 - 175, HDR_H + 110, 350, 70)) _openNewJobBuilder();
    return;
  }

  if (_inRect(tx, ty, 200, NAV_Y + 10, 130, 50)) { _openNewJobBuilder(); return; }

  if (pages > 1) {
    if (_inRect(tx, ty, 350, NAV_Y + 10, 60, 50)) {
      if (_schedPage > 0) { _schedPage--; needsRedraw = true; }
      return;
    }
    if (_inRect(tx, ty, 420, NAV_Y + 10, 60, 50)) {
      if (_schedPage < pages - 1) { _schedPage++; needsRedraw = true; }
      return;
    }
  }

  int start = _schedPage * SL_ROWS_PER_PAGE;
  for (int r = 0; r < SL_ROWS_PER_PAGE && start + r < total; r++) {
    int slot = idx[start + r];
    int y    = SL_LIST_Y + r * (SL_ROW_H + SL_ROW_GAP);
    Job& j   = jobs[slot];

    // RUN / STOP
    if (_inRect(tx, ty, SL_RUN_X, y + 9, 120, SL_ROW_H - 18)) {
      if (jobIsRunning(j)) stopJob();
      else                 startJob(j);
      needsRedraw = true;
      return;
    }

    // ON / OFF — a disabled job stays in the list but never fires
    if (_inRect(tx, ty, SL_TOG_X, y + 9, SL_TOG_W, SL_ROW_H - 18)) {
      j.enabled = !j.enabled;
      if (!j.enabled && jobIsRunning(j)) stopJob();
      storage::saveAllJobs();
      needsRedraw = true;
      return;
    }

    // Row body → edit screen
    if (_inRect(tx, ty, 10, y, SCR_W - 20, SL_ROW_H)) {
      _selectedJobIndex = slot;
      _loadEditState();
      currentScreen = SCR_EDIT;
      needsRedraw = true;
      return;
    }
  }
}

// ── EDIT screen ────────────────────────────────────────────────────────────
// Shows the full zone-step list, day-of-week toggles, time, and DELETE, for
// the job selected on the list screen (_selectedJobIndex).
//
// Selecting no days makes the job a one-shot run command; selecting any day
// promotes it to a repeating schedule. See _saveEditState().
//
// Origin handling: `origin` is "server" (pushed from the app / recommendation
// engine — complex multi-zone soak structure, treat steps as read-only here,
// user edits it in the app) or "device" (built locally via SCR_NEW_SCHEDULE →
// SCR_NEW_DAYS — simple shape, all changes safe). Both flavours allow editing
// time + days + DELETE on this screen. Per-step zone edits are not implemented
// yet for either; when they are, gate them on origin == "device" so
// server-owned structures aren't accidentally flattened by the on-device
// editor.

#define ED_STEPS_Y    (HDR_H + 30)   // y of the "ZONES (...)" header row
#define ED_ROW_OFF    30             // first step row sits this far below header
#define ED_STEP_H     28
#define ED_STEPS_MAX  4              // 4 steps per page (leaves room for days)
#define ED_DAYS_Y     (HDR_H + 170)  // 7 day toggles (35 tall)
#define ED_BTN_Y      (HDR_H + 215)  // y of time row
#define ED_BTN_H      50

void _drawEditScreen() {
  gfx.fillScreen(C_BG);

  Job* jp = _selectedJob();
  if (!jp) {
    // The job vanished while this screen was open — deleted from the app, or
    // retired by its own on_complete policy. Bounce back to the list.
    currentScreen = SCR_SCHEDULES;
    needsRedraw = true;
    return;
  }
  Job& job = *jp;

  char title[48];
  snprintf(title, sizeof(title), "EDIT: %s", job.name);
  _drawHeader(title);

  // Zone step list — paginated. ED_STEPS_MAX steps per page.
  int totalSteps = job.stepCount;
  int totalPages = totalSteps == 0 ? 1 : (totalSteps + ED_STEPS_MAX - 1) / ED_STEPS_MAX;
  if (_editStepsPage >= totalPages) _editStepsPage = totalPages - 1;
  int startIdx = _editStepsPage * ED_STEPS_MAX;
  int endIdx   = startIdx + ED_STEPS_MAX;
  if (endIdx > totalSteps) endIdx = totalSteps;

  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, ED_STEPS_Y);
  gfx.print("ZONES (");
  gfx.print(totalSteps);
  gfx.print(" steps):");

  // Origin badge — small pill next to the step header. "FROM APP" for
  // server-pushed schedules (steps are the engine's, edit in the app),
  // "ON-DEVICE" for locally authored ones (simple shape, fully editable).
  bool isDeviceOrigin = (strcmp(job.origin, "device") == 0);
  const char* badgeText = isDeviceOrigin ? "ON-DEVICE" : "FROM APP";
  uint16_t    badgeBg   = isDeviceOrigin ? C_GREEN    : C_BLUE;
  uint16_t    badgeFg   = isDeviceOrigin ? C_BLACK    : C_WHITE;
  int badgeW = strlen(badgeText) * 6 + 14;
  int badgeX = 20 + 7 * 6 + String(totalSteps).length() * 6 + 9 * 6 + 8;
  gfx.fillRoundRect(badgeX, ED_STEPS_Y - 4, badgeW, 16, 4, badgeBg);
  gfx.setTextColor(badgeFg); gfx.setTextSize(1);
  gfx.setCursor(badgeX + 7, ED_STEPS_Y);
  gfx.print(badgeText);

  // Pagination controls on the right edge of the header row
  if (totalPages > 1) {
    char pageLbl[12];
    snprintf(pageLbl, sizeof(pageLbl), "Page %d/%d", _editStepsPage + 1, totalPages);
    gfx.setTextColor(C_DIM); gfx.setTextSize(1);
    int lw = strlen(pageLbl) * 6;
    gfx.setCursor(SCR_W - 200 - lw / 2, ED_STEPS_Y);
    gfx.print(pageLbl);
    if (_editStepsPage > 0)
      _btn(SCR_W - 280, ED_STEPS_Y - 6, 60, 30, C_GRAY, "<", 2);
    if (_editStepsPage < totalPages - 1)
      _btn(SCR_W - 110, ED_STEPS_Y - 6, 60, 30, C_GRAY, ">", 2);
  }

  for (int i = startIdx; i < endIdx; i++) {
    int row = i - startIdx;
    int y   = ED_STEPS_Y + ED_ROW_OFF + row * ED_STEP_H;
    char line[40];
    int zoneId = job.steps[i].zoneId;
    int mins   = job.steps[i].duration / 60;
    int secs   = job.steps[i].duration % 60;
    // Zone 0 is an idle step — a soak pause between cycles, not a valve.
    const char* what = (zoneId == 0) ? "Soak" : "Zone";
    if (zoneId == 0) {
      if (secs == 0) snprintf(line, sizeof(line), "  %d. %s:      %d min", i + 1, what, mins);
      else           snprintf(line, sizeof(line), "  %d. %s:      %d:%02d", i + 1, what, mins, secs);
    } else if (secs == 0) {
      snprintf(line, sizeof(line), "  %d. %s %d:  %d min", i + 1, what, zoneId, mins);
    } else {
      snprintf(line, sizeof(line), "  %d. %s %d:  %d:%02d", i + 1, what, zoneId, mins, secs);
    }
    gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
    gfx.setCursor(20, y);
    gfx.print(line);
  }

  // Days of week — tap to toggle each. This is the run-command/schedule
  // switch: no days selected means the job runs once and retires.
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, ED_DAYS_Y - 12);
  gfx.print(_editDaysMask == 0 ? "DAYS TO RUN:   (none = run once, then done)"
                               : "DAYS TO RUN:   (repeats weekly)");
  _drawDaysRow(ED_DAYS_Y, _editDaysMask);

  // Time row — start time (always shown; relevant when any day is selected)
  gfx.setTextColor(C_DIM); gfx.setTextSize(2);
  gfx.setCursor(20, ED_BTN_Y + (ED_BTN_H - 16) / 2);
  gfx.print("Start:");

  _btn(120, ED_BTN_Y, ED_BTN_H, ED_BTN_H, C_GRAY, "-");
  char hbuf[3]; snprintf(hbuf, sizeof(hbuf), "%02d", _editHour);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  gfx.setCursor(182, ED_BTN_Y + (ED_BTN_H - 24) / 2);
  gfx.print(hbuf);
  _btn(220, ED_BTN_Y, ED_BTN_H, ED_BTN_H, C_GRAY, "+");

  gfx.setTextSize(3); gfx.setTextColor(C_DIM);
  gfx.setCursor(282, ED_BTN_Y + (ED_BTN_H - 24) / 2);
  gfx.print(":");

  _btn(305, ED_BTN_Y, ED_BTN_H, ED_BTN_H, C_GRAY, "-");
  char mbuf[3]; snprintf(mbuf, sizeof(mbuf), "%02d", _editMin);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  gfx.setCursor(367, ED_BTN_Y + (ED_BTN_H - 24) / 2);
  gfx.print(mbuf);
  _btn(405, ED_BTN_Y, ED_BTN_H, ED_BTN_H, C_GRAY, "+");

  // Nav bar: CANCEL | SAVE | DELETE | RUN NOW
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(10,  NAV_Y + 10, 130, 50, C_GRAY,  "CANCEL", 2);
  _btn(150, NAV_Y + 10, 130, 50, C_BLUE,  "SAVE",   2);
  _btn(290, NAV_Y + 10, 130, 50, C_RED,   "DELETE", 2);
  _btn(600, NAV_Y + 10, 190, 50, C_GREEN, "RUN NOW", 2, C_BLACK);
  _drawStatusBar();
}

void _handleEditTouch(int tx, int ty) {
  // CANCEL
  if (_inRect(tx, ty, 10, NAV_Y + 10, 130, 50)) {
    currentScreen = SCR_SCHEDULES; needsRedraw = true; return;
  }
  // SAVE
  if (_inRect(tx, ty, 150, NAV_Y + 10, 130, 50)) {
    _saveEditState(); currentScreen = SCR_SCHEDULES; needsRedraw = true; return;
  }
  // DELETE
  if (_inRect(tx, ty, 290, NAV_Y + 10, 130, 50)) {
    Job* j = _selectedJob();
    if (j) {
      if (jobIsRunning(*j)) stopJob();
      jobInitDefaults(*j);          // frees the slot
      storage::saveAllJobs();
    }
    _selectedJobIndex = -1;
    currentScreen = SCR_SCHEDULES;
    needsRedraw = true;
    return;
  }
  // RUN NOW
  if (_inRect(tx, ty, 600, NAV_Y + 10, 190, 50)) {
    _saveEditState();
    Job* j = _selectedJob();
    if (j) startJob(*j);
    currentScreen = SCR_HOME; needsRedraw = true; return;
  }

  // Pagination (only valid when more than one page exists)
  Job* jp = _selectedJob();
  int steps = jp ? jp->stepCount : 0;
  int totalPages = steps == 0 ? 1 : (steps + ED_STEPS_MAX - 1) / ED_STEPS_MAX;
  if (totalPages > 1) {
    if (_editStepsPage > 0 &&
        _inRect(tx, ty, SCR_W - 280, ED_STEPS_Y - 6, 60, 30)) {
      _editStepsPage--; needsRedraw = true; return;
    }
    if (_editStepsPage < totalPages - 1 &&
        _inRect(tx, ty, SCR_W - 110, ED_STEPS_Y - 6, 60, 30)) {
      _editStepsPage++; needsRedraw = true; return;
    }
  }

  // Day-of-week toggles
  int day = _hitDay(tx, ty, ED_DAYS_Y);
  if (day >= 0) {
    _editDaysMask ^= (uint8_t)(1 << day);
    needsRedraw = true;
    return;
  }

  // Time controls
  if (_inRect(tx, ty, 120, ED_BTN_Y, ED_BTN_H, ED_BTN_H)) { _editHour = (_editHour + 23) % 24; needsRedraw = true; return; }
  if (_inRect(tx, ty, 220, ED_BTN_Y, ED_BTN_H, ED_BTN_H)) { _editHour = (_editHour +  1) % 24; needsRedraw = true; return; }
  if (_inRect(tx, ty, 305, ED_BTN_Y, ED_BTN_H, ED_BTN_H)) { _editMin  = (_editMin  + 55) % 60; needsRedraw = true; return; }
  if (_inRect(tx, ty, 405, ED_BTN_Y, ED_BTN_H, ED_BTN_H)) { _editMin  = (_editMin  +  5) % 60; needsRedraw = true; return; }
}

// ── ZONE SETTINGS screen ───────────────────────────────────────────────────

#define ZS_ROW_H   75   // 4 rows × 75 = 300px, fits in the 320px content area
#define ZS_BTN_H   44   // button height inside each row
#define ZS_TOG_X    15
#define ZS_TOG_W    90
#define ZS_DEC_X   390
#define ZS_DEC_W    50
#define ZS_VAL_X   445
#define ZS_VAL_W    80
#define ZS_INC_X   530
#define ZS_INC_W    50


void _drawZoneSettingsScreen() {
  gfx.fillScreen(C_BG);

  // Page indicator in header title
  int maxPage = (ZONE_COUNT - 1) / ZS_ZONES_PER_PAGE;
  char title[32];
  snprintf(title, sizeof(title), "ZONE SETTINGS (%d/%d)", _zoneSettingsPage + 1, maxPage + 1);
  _drawHeader(title);

  // Draw only the zones on this page
  int zStart = _zoneSettingsPage * ZS_ZONES_PER_PAGE;
  int zEnd   = min(zStart + ZS_ZONES_PER_PAGE, ZONE_COUNT);
  for (int z = zStart; z < zEnd; z++) {
    // Re-map to screen row 0-3 so rows always start at the top
    int screenRow = z - zStart;
    int y   = HDR_H + screenRow * ZS_ROW_H;
    int bty = y + (ZS_ROW_H - ZS_BTN_H) / 2;
    bool en = zoneEnabled[z];
    gfx.fillRect(0, y, SCR_W, ZS_ROW_H, screenRow % 2 == 0 ? C_ROW_A : C_ROW_B);

    _btn(ZS_TOG_X, bty, ZS_TOG_W, ZS_BTN_H, en ? C_GREEN : C_GRAY, en ? "ON" : "OFF", 2, en ? C_BLACK : C_WHITE);

    gfx.setTextColor(en ? C_WHITE : C_DIM);
    gfx.setTextSize(3);
    char lbl[8];
    snprintf(lbl, sizeof(lbl), "Zone %d", z + 1);
    gfx.setCursor(115, y + (ZS_ROW_H - 24) / 2);
    gfx.print(lbl);

    if (en) {
      _btn(ZS_DEC_X, bty, ZS_DEC_W, ZS_BTN_H, C_GRAY, "-", 2);
      char rate[6];
      formatRate(zoneRate[z], rate, sizeof(rate));
      gfx.setTextColor(C_WHITE);
      gfx.setTextSize(2);
      int tw = strlen(rate) * 12;
      gfx.setCursor(ZS_VAL_X + (ZS_VAL_W - tw) / 2, y + (ZS_ROW_H - 16) / 2);
      gfx.print(rate);
      _btn(ZS_INC_X, bty, ZS_INC_W, ZS_BTN_H, C_GRAY, "+", 2);
      gfx.setTextColor(C_DIM);
      gfx.setTextSize(1);
      gfx.setCursor(ZS_INC_X + ZS_INC_W + 8, y + (ZS_ROW_H - 8) / 2);
      gfx.print("in/hr");
    } else {
      gfx.setTextColor(C_DIM);
      gfx.setTextSize(1);
      gfx.setCursor(ZS_DEC_X, y + (ZS_ROW_H - 8) / 2);
      gfx.print("disabled");
    }
  }

  // Nav bar: BACK | PREV | NEXT
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20, NAV_Y + 10, 160, 50, C_GRAY, "BACK");
  if (_zoneSettingsPage > 0)
    _btn(490, NAV_Y + 10, 130, 50, C_GRAY, "< PREV");
  if (_zoneSettingsPage < maxPage)
    _btn(640, NAV_Y + 10, 130, 50, C_GRAY, "NEXT >");

  _drawStatusBar();
}

void _handleZoneSettingsTouch(int tx, int ty) {
  int maxPage = (ZONE_COUNT - 1) / ZS_ZONES_PER_PAGE;

  // Nav buttons
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    _zoneSettingsPage = 0;   // reset to first page on exit
    currentScreen = SCR_HOME; needsRedraw = true; return;
  }
  if (_zoneSettingsPage > 0 && _inRect(tx, ty, 490, NAV_Y + 10, 130, 50)) {
    _zoneSettingsPage--; needsRedraw = true; return;
  }
  if (_zoneSettingsPage < maxPage && _inRect(tx, ty, 640, NAV_Y + 10, 130, 50)) {
    _zoneSettingsPage++; needsRedraw = true; return;
  }

  // Zone rows — only check zones on the current page
  int zStart = _zoneSettingsPage * ZS_ZONES_PER_PAGE;
  int zEnd   = min(zStart + ZS_ZONES_PER_PAGE, ZONE_COUNT);
  for (int z = zStart; z < zEnd; z++) {
    int screenRow = z - zStart;
    int y   = HDR_H + screenRow * ZS_ROW_H;
    int bty = y + (ZS_ROW_H - ZS_BTN_H) / 2;
    if (_inRect(tx, ty, ZS_TOG_X, bty, ZS_TOG_W, ZS_BTN_H)) {
      zoneEnabled[z] = !zoneEnabled[z];
      if (!zoneEnabled[z]) setZone(z, false);
      needsRedraw = true; return;
    }
    if (zoneEnabled[z]) {
      if (_inRect(tx, ty, ZS_DEC_X, bty, ZS_DEC_W, ZS_BTN_H)) {
        if (zoneRate[z] > 1) zoneRate[z]--;
        needsRedraw = true; return;
      }
      if (_inRect(tx, ty, ZS_INC_X, bty, ZS_INC_W, ZS_BTN_H)) {
        if (zoneRate[z] < 50) zoneRate[z]++;
        needsRedraw = true; return;
      }
    }
  }
}

// ── NEW JOB screen ────────────────────────────────────────────────────────
// Compose a simple job on-device:
//   • Start time (HH:MM)
//   • One global duration per zone
//   • 8 zone toggles (2 × 4 grid)
// Step 2 (SCR_NEW_DAYS) picks the days, which decides whether this becomes a
// one-shot run command or a repeating schedule. SAVE allocates a NEW job from
// the selected zones (in numeric order) using `_newDuration` for every step.

#define NS_TIME_Y    (HDR_H + 10)
#define NS_DUR_Y     (HDR_H + 75)
#define NS_GRID_Y    (HDR_H + 145)
#define NS_BTN_H     50

void _drawNewScheduleScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("NEW SCHEDULE");

  // ── Start time row ──
  gfx.setTextColor(C_DIM); gfx.setTextSize(2);
  gfx.setCursor(20, NS_TIME_Y + 15);
  gfx.print("Start:");

  _btn(105, NS_TIME_Y, NS_BTN_H, NS_BTN_H, C_GRAY, "-");
  char hbuf[3]; snprintf(hbuf, sizeof(hbuf), "%02d", _newHour);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  gfx.setCursor(167, NS_TIME_Y + (NS_BTN_H - 24) / 2);
  gfx.print(hbuf);
  _btn(205, NS_TIME_Y, NS_BTN_H, NS_BTN_H, C_GRAY, "+");

  gfx.setTextColor(C_DIM); gfx.setTextSize(3);
  gfx.setCursor(267, NS_TIME_Y + (NS_BTN_H - 24) / 2);
  gfx.print(":");

  _btn(290, NS_TIME_Y, NS_BTN_H, NS_BTN_H, C_GRAY, "-");
  char mbuf[3]; snprintf(mbuf, sizeof(mbuf), "%02d", _newMin);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  gfx.setCursor(352, NS_TIME_Y + (NS_BTN_H - 24) / 2);
  gfx.print(mbuf);
  _btn(390, NS_TIME_Y, NS_BTN_H, NS_BTN_H, C_GRAY, "+");

  // Right side of time row reserved (days are configured on the next screen)
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(525, NS_TIME_Y + 5);
  gfx.print("Pick days on the next screen");

  // ── Duration row ──
  gfx.setTextColor(C_DIM); gfx.setTextSize(2);
  gfx.setCursor(20, NS_DUR_Y + 15);
  gfx.print("Each zone:");

  _btn(190, NS_DUR_Y, NS_BTN_H, NS_BTN_H, C_GRAY, "-");
  char dbuf[5]; snprintf(dbuf, sizeof(dbuf), "%d", _newDuration);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  int dw = strlen(dbuf) * 18;
  gfx.setCursor(252 + (60 - dw) / 2, NS_DUR_Y + (NS_BTN_H - 24) / 2);
  gfx.print(dbuf);
  _btn(310, NS_DUR_Y, NS_BTN_H, NS_BTN_H, C_GRAY, "+");

  gfx.setTextColor(C_DIM); gfx.setTextSize(2);
  gfx.setCursor(375, NS_DUR_Y + 15);
  gfx.print("minutes");

  // ── Zone grid (2 rows × 4 cols) ──
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, NS_GRID_Y - 12);
  gfx.print("ZONES TO RUN:");

  const int cellW = 180;
  const int cellH = 75;
  const int gap   = 10;
  for (int i = 0; i < ZONE_COUNT; i++) {
    int col = i % 4;
    int row = i / 4;
    int x = 20 + col * (cellW + gap);
    int y = NS_GRID_Y + row * (cellH + gap);
    bool on = _newZones[i];
    char lbl[16];
    snprintf(lbl, sizeof(lbl), "Zone %d", i + 1);
    _btn(x, y, cellW, cellH, on ? C_GREEN : C_DARKGRAY,
         lbl, 2, on ? C_BLACK : C_WHITE);
  }

  // ── Nav ──
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 160, 50, C_GRAY,  "CANCEL");
  _btn(SCR_W - 200, NAV_Y + 10, 180, 50, C_BLUE,  "NEXT >", 2);

  // ── Status ──
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
  int selected = 0;
  for (int i = 0; i < ZONE_COUNT; i++) if (_newZones[i]) selected++;
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(10, STS_Y + 14);
  gfx.print(selected); gfx.print(" zone(s) selected");
  if (selected > 0) {
    gfx.print("  |  Total: ");
    gfx.print(selected * _newDuration);
    gfx.print(" min");
  }
}

void _handleNewScheduleTouch(int tx, int ty) {
  // CANCEL
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    currentScreen = SCR_SCHEDULES; needsRedraw = true; return;
  }
  // NEXT → continue to day picker. Require at least one zone selected.
  if (_inRect(tx, ty, SCR_W - 200, NAV_Y + 10, 180, 50)) {
    int n = 0;
    for (int i = 0; i < ZONE_COUNT; i++) if (_newZones[i]) n++;
    if (n == 0) return;   // nothing selected → ignore tap
    currentScreen = SCR_NEW_DAYS;
    needsRedraw = true;
    return;
  }

  // Start time controls
  if (_inRect(tx, ty, 105, NS_TIME_Y, NS_BTN_H, NS_BTN_H)) { _newHour = (_newHour + 23) % 24; needsRedraw = true; return; }
  if (_inRect(tx, ty, 205, NS_TIME_Y, NS_BTN_H, NS_BTN_H)) { _newHour = (_newHour +  1) % 24; needsRedraw = true; return; }
  if (_inRect(tx, ty, 290, NS_TIME_Y, NS_BTN_H, NS_BTN_H)) { _newMin  = (_newMin  + 55) % 60; needsRedraw = true; return; }
  if (_inRect(tx, ty, 390, NS_TIME_Y, NS_BTN_H, NS_BTN_H)) { _newMin  = (_newMin  +  5) % 60; needsRedraw = true; return; }

  // Duration -/+
  if (_inRect(tx, ty, 190, NS_DUR_Y, NS_BTN_H, NS_BTN_H)) {
    if (_newDuration > 1) _newDuration--;
    needsRedraw = true; return;
  }
  if (_inRect(tx, ty, 310, NS_DUR_Y, NS_BTN_H, NS_BTN_H)) {
    if (_newDuration < 120) _newDuration++;
    needsRedraw = true; return;
  }

  // Zone toggles
  const int cellW = 180;
  const int cellH = 75;
  const int gap   = 10;
  for (int i = 0; i < ZONE_COUNT; i++) {
    int col = i % 4;
    int row = i / 4;
    int x = 20 + col * (cellW + gap);
    int y = NS_GRID_Y + row * (cellH + gap);
    if (_inRect(tx, ty, x, y, cellW, cellH)) {
      _newZones[i] = !_newZones[i];
      needsRedraw = true;
      return;
    }
  }
}

// ── NEW SCHEDULE — step 2: pick days of week ──────────────────────────────
// User tapped NEXT on the previous screen. Pick which days the schedule
// should auto-run on (or none for manual only) and SAVE to commit.

#define ND_DAYS_Y    (HDR_H + 100)   // y of the day toggle row
#define ND_PRESETS_Y (HDR_H + 200)   // y of the preset shortcut buttons

void _drawNewDaysScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("WHEN TO RUN");

  // Summary of step 1's settings (read-only recap)
  int selected = 0;
  for (int i = 0; i < ZONE_COUNT; i++) if (_newZones[i]) selected++;
  char recap[80];
  snprintf(recap, sizeof(recap), "Starts %02d:%02d  -  %d zone(s) x %d min  =  %d min total",
           _newHour, _newMin, selected, _newDuration, selected * _newDuration);
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, HDR_H + 15);
  gfx.print(recap);

  // Section label
  gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
  gfx.setCursor(20, HDR_H + 50);
  gfx.print("Pick days to auto-run:");
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, HDR_H + 75);
  gfx.print("(leave all off for manual-only)");

  // 7 day toggles
  _drawDaysRow(ND_DAYS_Y, _newDaysMask);

  // Preset shortcut buttons
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, ND_PRESETS_Y - 12);
  gfx.print("PRESETS:");
  const int pw = 170, ph = 50, pgap = 15;
  _btn(20,                       ND_PRESETS_Y, pw, ph, C_GRAY, "Every day", 2);
  _btn(20 + (pw + pgap) * 1,     ND_PRESETS_Y, pw, ph, C_GRAY, "Weekdays",  2);
  _btn(20 + (pw + pgap) * 2,     ND_PRESETS_Y, pw, ph, C_GRAY, "Weekends",  2);
  _btn(20 + (pw + pgap) * 3,     ND_PRESETS_Y, pw, ph, C_GRAY, "Clear all", 2);

  // Selected summary at bottom
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
  char summary[48];
  _formatDaysMask(_newDaysMask, summary, sizeof(summary));
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(10, STS_Y + 14);
  gfx.print("Runs: "); gfx.print(summary);

  // Nav: BACK | SAVE
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 160, 50, C_GRAY,  "< BACK", 2);
  _btn(SCR_W - 200, NAV_Y + 10, 180, 50, C_GREEN, "SAVE",   2, C_BLACK);
}

void _handleNewDaysTouch(int tx, int ty) {
  // BACK
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    currentScreen = SCR_NEW_SCHEDULE;
    needsRedraw = true; return;
  }
  // SAVE — commit the builder state as a NEW job (existing jobs are untouched)
  if (_inRect(tx, ty, SCR_W - 200, NAV_Y + 10, 180, 50)) {
    int selected = 0;
    for (int i = 0; i < ZONE_COUNT; i++) if (_newZones[i]) selected++;
    if (selected == 0) {
      // shouldn't happen — NEXT button is gated on n > 0, but be defensive
      currentScreen = SCR_NEW_SCHEDULE; needsRedraw = true; return;
    }

    Job* j = allocJob();
    if (!j) {
      // All slots full. Say so rather than silently discarding the work.
      gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
      gfx.setTextColor(C_RED); gfx.setTextSize(1);
      gfx.setCursor(10, STS_Y + 14);
      gfx.print("Job storage full - delete one first");
      return;
    }

    jobInitDefaults(*j);
    j->used      = true;
    j->id        = nextJobId++;
    j->enabled   = true;
    j->runHour   = _newHour;
    j->runMinute = _newMin;
    j->daysMask  = _newDaysMask;

    // No days picked means "run once". Give it a concrete fire time — the
    // next occurrence of the chosen clock time — and let it retire afterwards.
    if (_newDaysMask == 0) {
      j->repeat     = REPEAT_NONE;
      j->onComplete = ON_COMPLETE_DELETE;
      if (ntpReady) {
        uint32_t today = (ntpEpoch() / 86400UL) * 86400UL;
        uint32_t fire  = today + (uint32_t)_newHour * 3600UL
                               + (uint32_t)_newMin * 60UL;
        if (fire <= (uint32_t)ntpEpoch()) fire += 86400UL;
        j->runAtEpoch = fire;
      }
    } else {
      j->repeat     = (_newDaysMask == 0x7F) ? REPEAT_DAILY : REPEAT_WEEKLY;
      j->onComplete = ON_COMPLETE_KEEP;
    }

    int n = 0;
    for (int i = 0; i < ZONE_COUNT && n < JOB_MAX_STEPS; i++) {
      if (_newZones[i]) {
        j->steps[n].zoneId   = i + 1;
        j->steps[n].duration = _newDuration * 60;
        n++;
      }
    }
    j->stepCount = n;

    snprintf(j->name, sizeof(j->name), "On-device %d", j->id);
    // Mark this job as locally authored. The EDIT screen surfaces this so the
    // user knows the simple all-zones-same-duration shape is editable here,
    // vs. server jobs which should be edited in the app.
    strncpy(j->origin, "device", sizeof(j->origin) - 1);
    j->origin[sizeof(j->origin) - 1] = '\0';

    storage::saveAllJobs();
    currentScreen = SCR_SCHEDULES;
    needsRedraw = true;
    return;
  }

  // Preset buttons
  const int pw = 170, ph = 50, pgap = 15;
  for (int i = 0; i < 4; i++) {
    int x = 20 + (pw + pgap) * i;
    if (_inRect(tx, ty, x, ND_PRESETS_Y, pw, ph)) {
      switch (i) {
        case 0: _newDaysMask = 0x7F; break;   // Every day
        case 1: _newDaysMask = 0x3E; break;   // Weekdays (Mon-Fri)
        case 2: _newDaysMask = 0x41; break;   // Weekends (Sun+Sat)
        case 3: _newDaysMask = 0x00; break;   // Clear
      }
      needsRedraw = true;
      return;
    }
  }

  // Individual day toggles
  int day = _hitDay(tx, ty, ND_DAYS_Y);
  if (day >= 0) {
    _newDaysMask ^= (uint8_t)(1 << day);
    needsRedraw = true;
    return;
  }
}

// ── Public API ─────────────────────────────────────────────────────────────

void displayInit() {
  gfx.begin();
  gfx.setRotation(3);
  gfx.fillScreen(C_BG);
  _backlight.begin();
  _backlight.set(255);
  touch.begin();
  touch.onDetect(_onTouch);
  needsRedraw     = true;
  _lastActivityMs = millis();
}

// Show a full-screen status message during startup (before loop() runs).
void displayStatus(const char* line1, const char* line2 = nullptr) {
  gfx.fillScreen(C_BLACK);
  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(3);
  int tw = strlen(line1) * 18;
  gfx.setCursor((SCR_W - tw) / 2, SCR_H / 2 - 30);
  gfx.print(line1);
  if (line2) {
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(2);
    tw = strlen(line2) * 12;
    gfx.setCursor((SCR_W - tw) / 2, SCR_H / 2 + 20);
    gfx.print(line2);
  }
}

void displayLoop() {
  // Apply a pending wake requested from the WiFi handler (deferred to avoid hardware conflicts)
  if (_pendingWake) {
    _pendingWake   = false;
    _displayAsleep = false;
    _backlight.set(255);
    needsRedraw    = true;
  }

  // Full redraw only when changing screens or on first boot
  if (needsRedraw) {
    switch (currentScreen) {
      case SCR_HOME:             _drawHomeScreen();                  break;
      case SCR_SCHEDULES:        _drawSchedulesScreen();             break;
      case SCR_EDIT:             _drawEditScreen();                  break;
      case SCR_ZONE_SETTINGS:    _drawZoneSettingsScreen();          break;
      case SCR_NEW_SCHEDULE:     _drawNewScheduleScreen();           break;
      case SCR_NEW_DAYS:         _drawNewDaysScreen();               break;
      case SCR_PAIRING:          _drawPairingScreen();               break;
      case SCR_WIFI_LIST:        wifi_setup::drawListScreen();       break;
      case SCR_WIFI_PASSWORD:    wifi_setup::drawPasswordScreen();   break;
      case SCR_WIFI_CONNECTING:  wifi_setup::drawConnectingScreen(); break;
      case SCR_SETTINGS:         settings::drawSettingsScreen();     break;
      case SCR_ABOUT:            settings::drawAboutScreen();        break;
    }
    needsRedraw  = false;
    _lastTime    = ntpReady ? ntpFormattedTime().substring(0, 5) : "";
    _lastRunning = runState.running;
  }

  // Update the clock region in the header when the minute changes
  String nowTime = ntpReady ? ntpFormattedTime().substring(0, 5) : "";
  if (nowTime != _lastTime) {
    _lastTime = nowTime;
    _updateClockDisplay();
  }

  // Update the status bar when run state starts or stops
  if (runState.running != _lastRunning) {
    _lastRunning = runState.running;
    _drawStatusBar();
  }

  // Refresh status bar every second while a schedule is running
  if (runState.running) {
    static unsigned long _lastSts = 0;
    if (millis() - _lastSts >= 1000) { _drawStatusBar(); _lastSts = millis(); }
  }

  // Finger-up detection: timestamp _contactSeen pulses here (safe millis context),
  // then release _fingerDown if the IC has been quiet for 300ms.
  static unsigned long _lastContactSeenMs = 0;
  if (_contactSeen) {
    _lastContactSeenMs = millis();
    _contactSeen = false;
  }
  if (_fingerDown && millis() - _lastContactSeenMs > 150) {
    _fingerDown = false;
  }

  // Consume touch event — ignore taps within 100ms of the last one
  static unsigned long _lastTouchMs = 0;
  if (_touched) {
    _touched = false;
    if (_displayAsleep) {
      wakeDisplay();   // first tap only wakes; does not trigger an action
    } else if (millis() - _lastTouchMs > 100) {
      _lastTouchMs    = millis();
      _lastActivityMs = millis();
      int tx = _touchX, ty = _touchY;
      switch (currentScreen) {
        case SCR_HOME:             _handleHomeTouch(tx, ty);                 break;
        case SCR_SCHEDULES:        _handleSchedulesTouch(tx, ty);            break;
        case SCR_EDIT:             _handleEditTouch(tx, ty);                 break;
        case SCR_ZONE_SETTINGS:    _handleZoneSettingsTouch(tx, ty);         break;
        case SCR_NEW_SCHEDULE:     _handleNewScheduleTouch(tx, ty);          break;
        case SCR_NEW_DAYS:         _handleNewDaysTouch(tx, ty);              break;
        case SCR_PAIRING:          _handlePairingTouch(tx, ty);              break;
        case SCR_WIFI_LIST:        wifi_setup::handleListTouch(tx, ty);      break;
        case SCR_WIFI_PASSWORD:    wifi_setup::handlePasswordTouch(tx, ty);  break;
        case SCR_WIFI_CONNECTING:  /* no-op: connect happens after draw */   break;
        case SCR_SETTINGS:         settings::handleSettingsTouch(tx, ty);    break;
        case SCR_ABOUT:            settings::handleAboutTouch(tx, ty);       break;
      }
    }
  }

  // Sleep the backlight after SLEEP_TIMEOUT_SEC of inactivity (0 = never)
  if (!_displayAsleep && SLEEP_TIMEOUT_SEC > 0 &&
      millis() - _lastActivityMs > (unsigned long)SLEEP_TIMEOUT_SEC * 1000UL) {
    _sleepDisplay();
  }
}
