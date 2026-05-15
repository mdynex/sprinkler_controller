#pragma once
#include <Arduino_GigaDisplay_GFX.h>
#include <Arduino_GigaDisplayTouch.h>
#include <Arduino_GigaDisplay.h>
#include "zones.h"
#include "schedules.h"
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

// Edit: zone list rows
#define ED_ROW_H    35
#define ED_START_Y  HDR_H     // rows start immediately after header
#define ED_SETT_Y  (HDR_H + ZONE_COUNT * ED_ROW_H)   // 330
// Edit row column x positions
#define ED_TOG_X    15
#define ED_TOG_W    65
#define ED_LBL_X    90
#define ED_DEC_X   390
#define ED_DEC_W    45
#define ED_DUR_X   440
#define ED_DUR_W    70
#define ED_INC_X   515
#define ED_INC_W    45
#define ED_UNIT_X  565

// ── Global objects ─────────────────────────────────────────────────────────
GigaDisplay_GFX          gfx;
Arduino_GigaDisplayTouch touch;
GigaDisplayBacklight     _backlight;

// ── Screen state ───────────────────────────────────────────────────────────
enum Screen { SCR_HOME, SCR_SCHEDULES, SCR_EDIT, SCR_ZONE_SETTINGS };
Screen        currentScreen   = SCR_HOME;
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
struct EditZone {
  bool enabled;
  int  durationMin;   // 1–99 minutes
};

bool     _editAutoRun = false;
int      _editHour    = 6;
int      _editMin     = 0;
EditZone _editZones[ZONE_COUNT];

// ── Zone Settings paging ───────────────────────────────────────────────────
#define ZS_ZONES_PER_PAGE  4
int _zoneSettingsPage = 0;   // 0 = zones 1-4, 1 = zones 5-8, etc.

void _loadEditState() {
  _editAutoRun = theSchedule.autoRun;
  _editHour    = theSchedule.runHour;
  _editMin     = theSchedule.runMinute;
  for (int z = 0; z < ZONE_COUNT; z++) {
    _editZones[z] = {false, 5};
  }
  for (int st = 0; st < theSchedule.stepCount; st++) {
    int z = theSchedule.steps[st].zoneId - 1;
    if (z >= 0 && z < ZONE_COUNT) {
      _editZones[z].enabled     = true;
      _editZones[z].durationMin = max(1, theSchedule.steps[st].duration / 60);
    }
  }
}

void _saveEditState() {
  theSchedule.autoRun   = _editAutoRun;
  theSchedule.runHour   = _editHour;
  theSchedule.runMinute = _editMin;
  theSchedule.stepCount = 0;
  for (int z = 0; z < ZONE_COUNT; z++) {
    if (!_editZones[z].enabled) continue;
    if (theSchedule.stepCount >= SCHEDULE_MAX_STEPS) break;
    theSchedule.steps[theSchedule.stepCount++] = {z + 1, _editZones[z].durationMin * 60};
  }
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

void _drawHeader(const char* title) {
  gfx.fillRect(0, 0, SCR_W, HDR_H, C_BLACK);
  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(2);
  gfx.setCursor(15, 17);
  gfx.print(title);
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
  if (runState.running) {
    ZoneStep& step = theSchedule.steps[runState.stepIndex];
    unsigned long elapsed   = (millis() - runState.stepStart) / 1000UL;
    unsigned long remaining = step.duration > (int)elapsed ? step.duration - elapsed : 0;
    gfx.print("Running: ");
    gfx.print(theSchedule.name);
    gfx.print("  |  Zone ");
    gfx.print(step.zoneId);
    gfx.print("  |  Step ");
    gfx.print(runState.stepIndex + 1);
    gfx.print("/");
    gfx.print(theSchedule.stepCount);
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
    stopSchedule();
    for (int i = 0; i < ZONE_COUNT; i++) _drawZoneBtn(i);
    _drawStatusBar();
  }
}

// ── SCHEDULES screen ───────────────────────────────────────────────────────

#define SL_ROW_H    55
#define SL_ROW_GAP   8
#define SL_LIST_Y   (HDR_H + 5)
#define SL_RUN_X    (SCR_W - 140)

void _drawSchedRow(int screenRow) {
  int y = SL_LIST_Y + screenRow * (SL_ROW_H + SL_ROW_GAP);
  bool active = runState.running;
  gfx.fillRoundRect(10, y, SCR_W - 20, SL_ROW_H, 6, active ? 0x0343 : C_DARKGRAY);

  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(2);
  gfx.setCursor(20, y + 10);
  gfx.print(theSchedule.name);

  gfx.setTextSize(1);
  gfx.setTextColor(theSchedule.autoRun ? C_ORANGE : C_DIM);
  gfx.setCursor(20, y + 36);
  if (theSchedule.autoRun) {
    char buf[12];
    snprintf(buf, sizeof(buf), "Daily %02d:%02d", theSchedule.runHour, theSchedule.runMinute);
    gfx.print(buf);
  } else {
    gfx.print("Manual only");
  }

  gfx.setTextColor(C_DIM);
  gfx.setCursor(270, y + 22);
  gfx.print(theSchedule.stepCount);
  gfx.print(" zones");

  _btn(SL_RUN_X, y + 9, 120, SL_ROW_H - 18,
       active ? C_RED : C_GREEN,
       active ? "STOP" : "RUN", 2,
       active ? C_WHITE : C_BLACK);
}

void _drawSchedulesScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("SCHEDULES");
  if (theSchedule.used) {
    _drawSchedRow(0);
  } else {
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(2);
    gfx.setCursor(20, HDR_H + 40);
    gfx.print("No schedule yet.");
    gfx.setCursor(20, HDR_H + 70);
    gfx.print("Send one from the app, then tap it here to configure.");
  }
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 160, 50, C_GRAY, "BACK");
  _btn(SCR_W - 250, NAV_Y + 10, 230, 50, C_RED,  "STOP ALL");
  _drawStatusBar();
}

void _handleSchedulesTouch(int tx, int ty) {
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) { currentScreen = SCR_HOME; needsRedraw = true; return; }

  // STOP ALL button
  if (_inRect(tx, ty, SCR_W - 250, NAV_Y + 10, 230, 50)) {
    stopSchedule();
    if (theSchedule.used) _drawSchedRow(0);
    _drawStatusBar();
    return;
  }

  if (!theSchedule.used) return;
  int y = SL_LIST_Y;

  // Run/Stop button — redraw just the row + status bar
  if (_inRect(tx, ty, SL_RUN_X, y + 9, 120, SL_ROW_H - 18)) {
    if (runState.running) stopSchedule();
    else startSchedule();
    _drawSchedRow(0);
    _drawStatusBar();
    return;
  }
  // Row body → edit screen
  if (_inRect(tx, ty, 10, y, SCR_W - 20, SL_ROW_H)) {
    _loadEditState();
    currentScreen = SCR_EDIT;
    needsRedraw = true;
  }
}

// ── EDIT screen ────────────────────────────────────────────────────────────

void _drawEditZoneRow(int z) {
  int y   = ED_START_Y + z * ED_ROW_H;
  int bty = y + (ED_ROW_H - 28) / 2;
  gfx.fillRect(0, y, SCR_W, ED_ROW_H, z % 2 == 0 ? C_ROW_A : C_ROW_B);

  // Zone toggle
  _btn(ED_TOG_X, bty, ED_TOG_W, 28,
       _editZones[z].enabled ? C_GREEN : C_GRAY,
       _editZones[z].enabled ? "ON" : "OFF", 1,
       _editZones[z].enabled ? C_BLACK : C_WHITE);

  // Label
  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(2);
  gfx.setCursor(ED_LBL_X, y + (ED_ROW_H - 16) / 2);
  char lbl[8];
  snprintf(lbl, sizeof(lbl), "Zone %d", z + 1);
  gfx.print(lbl);

  if (_editZones[z].enabled) {
    // Duration [-] value [+] min
    _btn(ED_DEC_X, bty, ED_DEC_W, 28, C_GRAY, "-");
    gfx.setTextColor(C_WHITE);
    gfx.setTextSize(2);
    char dur[4];
    snprintf(dur, sizeof(dur), "%d", _editZones[z].durationMin);
    int tw = strlen(dur) * 12;
    gfx.setCursor(ED_DUR_X + (ED_DUR_W - tw) / 2, y + (ED_ROW_H - 16) / 2);
    gfx.print(dur);
    _btn(ED_INC_X, bty, ED_INC_W, 28, C_GRAY, "+");
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(1);
    gfx.setCursor(ED_UNIT_X, y + (ED_ROW_H - 8) / 2);
    gfx.print("min");
  } else {
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(1);
    gfx.setCursor(ED_DEC_X, y + (ED_ROW_H - 8) / 2);
    gfx.print("tap ON to enable");
  }
}

void _drawEditSettings() {
  // Settings bar sits between zone rows and nav bar (y=330 to y=370)
  int y   = ED_SETT_Y;
  int h   = NAV_Y - ED_SETT_Y;   // remaining space = 40px
  int bty = y + (h - 26) / 2;
  gfx.fillRect(0, y, SCR_W, h, C_BLACK);

  _btn(15, bty, 90, 26,
       _editAutoRun ? C_GREEN : C_GRAY,
       _editAutoRun ? "AUTO ON" : "AUTO OFF", 1,
       _editAutoRun ? C_BLACK : C_WHITE);

  if (_editAutoRun) {
    gfx.setTextColor(C_WHITE);
    gfx.setTextSize(1);
    gfx.setCursor(115, y + (h - 8) / 2);
    gfx.print("Run at:");

    // Hour
    _btn(175, bty, 35, 26, C_GRAY, "-", 1);
    gfx.setTextColor(C_WHITE);
    gfx.setTextSize(2);
    char hbuf[3];
    snprintf(hbuf, sizeof(hbuf), "%02d", _editHour);
    gfx.setCursor(215, y + (h - 16) / 2);
    gfx.print(hbuf);
    _btn(245, bty, 35, 26, C_GRAY, "+", 1);

    gfx.setTextColor(C_WHITE);
    gfx.setTextSize(2);
    gfx.setCursor(285, y + (h - 16) / 2);
    gfx.print(":");

    // Minute
    _btn(300, bty, 35, 26, C_GRAY, "-", 1);
    char mbuf[3];
    snprintf(mbuf, sizeof(mbuf), "%02d", _editMin);
    gfx.setCursor(340, y + (h - 16) / 2);
    gfx.print(mbuf);
    _btn(370, bty, 35, 26, C_GRAY, "+", 1);
  }
}

void _drawEditScreen() {
  gfx.fillScreen(C_BG);
  char title[48];
  snprintf(title, sizeof(title), "EDIT: %s", theSchedule.name);
  _drawHeader(title);

  for (int z = 0; z < ZONE_COUNT; z++) _drawEditZoneRow(z);
  _drawEditSettings();

  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 160, 50, C_GRAY,  "CANCEL");
  _btn(320,         NAV_Y + 10, 160, 50, C_BLUE,  "SAVE");
  _btn(SCR_W - 200, NAV_Y + 10, 180, 50, C_GREEN, "RUN NOW", 2, C_BLACK);
  _drawStatusBar();
}

void _handleEditTouch(int tx, int ty) {
  // Nav
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    currentScreen = SCR_SCHEDULES; needsRedraw = true; return;
  }
  if (_inRect(tx, ty, 320, NAV_Y + 10, 160, 50)) {
    _saveEditState();
    currentScreen = SCR_SCHEDULES; needsRedraw = true; return;
  }
  if (_inRect(tx, ty, SCR_W - 200, NAV_Y + 10, 180, 50)) {
    _saveEditState();
    startSchedule();
    currentScreen = SCR_HOME; needsRedraw = true; return;
  }

  // Settings bar
  int sy   = ED_SETT_Y;
  int sh   = NAV_Y - ED_SETT_Y;
  int sbty = sy + (sh - 26) / 2;
  if (_inRect(tx, ty, 15, sbty, 90, 26)) {
    _editAutoRun = !_editAutoRun; _drawEditSettings(); return;
  }
  if (_editAutoRun) {
    if (_inRect(tx, ty, 175, sbty, 35, 26)) { _editHour = (_editHour + 23) % 24; _drawEditSettings(); return; }
    if (_inRect(tx, ty, 245, sbty, 35, 26)) { _editHour = (_editHour +  1) % 24; _drawEditSettings(); return; }
    if (_inRect(tx, ty, 300, sbty, 35, 26)) { _editMin  = (_editMin  + 55) % 60; _drawEditSettings(); return; }
    if (_inRect(tx, ty, 370, sbty, 35, 26)) { _editMin  = (_editMin  +  5) % 60; _drawEditSettings(); return; }
  }

  // Zone rows
  for (int z = 0; z < ZONE_COUNT; z++) {
    int y   = ED_START_Y + z * ED_ROW_H;
    int bty = y + (ED_ROW_H - 28) / 2;
    if (_inRect(tx, ty, ED_TOG_X, bty, ED_TOG_W, 28)) {
      _editZones[z].enabled = !_editZones[z].enabled;
      _drawEditZoneRow(z); return;
    }
    if (_editZones[z].enabled) {
      if (_inRect(tx, ty, ED_DEC_X, bty, ED_DEC_W, 28)) {
        if (_editZones[z].durationMin > 1) _editZones[z].durationMin--;
        _drawEditZoneRow(z); return;
      }
      if (_inRect(tx, ty, ED_INC_X, bty, ED_INC_W, 28)) {
        if (_editZones[z].durationMin < 99) _editZones[z].durationMin++;
        _drawEditZoneRow(z); return;
      }
    }
  }
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
      case SCR_HOME:          _drawHomeScreen();          break;
      case SCR_SCHEDULES:     _drawSchedulesScreen();     break;
      case SCR_EDIT:          _drawEditScreen();          break;
      case SCR_ZONE_SETTINGS: _drawZoneSettingsScreen();  break;
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
        case SCR_HOME:          _handleHomeTouch(tx, ty);          break;
        case SCR_SCHEDULES:     _handleSchedulesTouch(tx, ty);     break;
        case SCR_EDIT:          _handleEditTouch(tx, ty);          break;
        case SCR_ZONE_SETTINGS: _handleZoneSettingsTouch(tx, ty);  break;
      }
    }
  }

  // Sleep the backlight after SLEEP_TIMEOUT_SEC of inactivity (0 = never)
  if (!_displayAsleep && SLEEP_TIMEOUT_SEC > 0 &&
      millis() - _lastActivityMs > (unsigned long)SLEEP_TIMEOUT_SEC * 1000UL) {
    _sleepDisplay();
  }
}
