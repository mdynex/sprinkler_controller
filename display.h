#pragma once
#include <Arduino_GigaDisplay_GFX.h>
#include <Arduino_GigaDisplayTouch.h>
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

// ── Screen state ───────────────────────────────────────────────────────────
enum Screen { SCR_HOME, SCR_SCHEDULES, SCR_EDIT };
Screen currentScreen = SCR_HOME;
bool   needsRedraw   = true;
String _lastTime     = "";
bool   _lastRunning  = false;

// Touch event — set by callback, consumed in displayLoop()
volatile bool _touched = false;
volatile int  _touchX  = 0;
volatile int  _touchY  = 0;

void _onTouch(uint8_t contacts, GDTpoint_t* pts) {
  if (contacts > 0 && !_touched) {
    _touchX  = SCR_W - pts[0].y;   // flipped landscape X
    _touchY  = pts[0].x;            // flipped landscape Y
    _touched = true;
  }
}

// ── Edit screen state ──────────────────────────────────────────────────────
struct EditZone {
  bool enabled;
  int  durationMin;   // 1–99 minutes
};

int      _editIdx     = -1;
bool     _editAutoRun = false;
int      _editHour    = 6;
int      _editMin     = 0;
EditZone _editZones[ZONE_COUNT];

void _loadEditState(int schedIdx) {
  _editIdx     = schedIdx;
  Schedule& s  = schedules[schedIdx];
  _editAutoRun = s.autoRun;
  _editHour    = s.runHour;
  _editMin     = s.runMinute;
  for (int z = 0; z < ZONE_COUNT; z++) {
    _editZones[z] = {false, 5};
  }
  for (int st = 0; st < s.stepCount; st++) {
    int z = s.steps[st].zoneId - 1;
    if (z >= 0 && z < ZONE_COUNT) {
      _editZones[z].enabled     = true;
      _editZones[z].durationMin = max(1, s.steps[st].duration / 60);
    }
  }
}

void _saveEditState() {
  Schedule& s  = schedules[_editIdx];
  s.autoRun    = _editAutoRun;
  s.runHour    = _editHour;
  s.runMinute  = _editMin;
  s.stepCount  = 0;
  for (int z = 0; z < ZONE_COUNT; z++) {
    if (!_editZones[z].enabled) continue;
    if (s.stepCount >= MAX_ZONE_STEPS) break;
    s.steps[s.stepCount++] = {z + 1, _editZones[z].durationMin * 60};
  }
}

// ── Drawing utilities ──────────────────────────────────────────────────────

bool _inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

// Draws a rounded button with centered label. sz = GFX text size (1 or 2).
void _btn(int x, int y, int w, int h, uint16_t color, const char* lbl, int sz = 2) {
  gfx.fillRoundRect(x, y, w, h, 6, color);
  gfx.setTextColor(C_WHITE);
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
    Schedule& s    = schedules[runState.scheduleIndex];
    ZoneStep& step = s.steps[runState.stepIndex];
    unsigned long elapsed   = (millis() - runState.stepStart) / 1000UL;
    unsigned long remaining = step.duration > (int)elapsed ? step.duration - elapsed : 0;
    gfx.print("Running: ");
    gfx.print(s.name);
    gfx.print("  |  Zone ");
    gfx.print(step.zoneId);
    gfx.print("  |  Step ");
    gfx.print(runState.stepIndex + 1);
    gfx.print("/");
    gfx.print(s.stepCount);
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
  uint16_t bg = zoneState[idx] ? C_GREEN : C_ZONE_OFF;
  gfx.fillRoundRect(x, y, ZN_W, ZN_H, 10, bg);
  gfx.drawRoundRect(x, y, ZN_W, ZN_H, 10, C_WHITE);
  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(3);
  char top[8];
  snprintf(top, sizeof(top), "Zn %d", idx + 1);
  int tw = strlen(top) * 18;
  gfx.setCursor(x + (ZN_W - tw) / 2, y + ZN_H / 2 - 24);
  gfx.print(top);
  gfx.setTextSize(2);
  const char* st = zoneState[idx] ? "ON" : "OFF";
  tw = strlen(st) * 12;
  gfx.setCursor(x + (ZN_W - tw) / 2, y + ZN_H / 2 + 8);
  gfx.print(st);
}

void _drawHomeScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("SPRINKLER CONTROLLER");
  for (int i = 0; i < ZONE_COUNT; i++) _drawZoneBtn(i);
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 230, 50, C_GRAY, "SCHEDULES");
  _btn(SCR_W - 250, NAV_Y + 10, 230, 50, C_RED,  "STOP ALL");
  _drawStatusBar();
}

void _handleHomeTouch(int tx, int ty) {
  for (int i = 0; i < ZONE_COUNT; i++) {
    int x = ZN_PAD + (i % ZN_COLS) * (ZN_W + ZN_PAD);
    int y = HDR_H  + ZN_PAD + (i / ZN_COLS) * (ZN_H + ZN_PAD);
    if (_inRect(tx, ty, x, y, ZN_W, ZN_H)) {
      setZone(i, !zoneState[i]);
      _drawZoneBtn(i);       // redraw just this button
      _drawStatusBar();
      return;
    }
  }
  if (_inRect(tx, ty, 20, NAV_Y + 10, 230, 50))          { currentScreen = SCR_SCHEDULES; needsRedraw = true; return; }
  if (_inRect(tx, ty, SCR_W - 250, NAV_Y + 10, 230, 50)) { stopSchedule(); _drawStatusBar(); }
}

// ── SCHEDULES screen ───────────────────────────────────────────────────────

#define SL_ROW_H    55
#define SL_ROW_GAP   8
#define SL_LIST_Y   (HDR_H + 5)
#define SL_RUN_X    (SCR_W - 140)

void _drawSchedRow(int screenRow, int si) {
  Schedule& s  = schedules[si];
  int y = SL_LIST_Y + screenRow * (SL_ROW_H + SL_ROW_GAP);
  bool active = runState.running && runState.scheduleIndex == si;
  gfx.fillRoundRect(10, y, SCR_W - 20, SL_ROW_H, 6, active ? 0x0343 : C_DARKGRAY);

  gfx.setTextColor(C_WHITE);
  gfx.setTextSize(2);
  gfx.setCursor(20, y + 10);
  gfx.print(s.name);

  gfx.setTextSize(1);
  gfx.setTextColor(s.autoRun ? C_ORANGE : C_DIM);
  gfx.setCursor(20, y + 36);
  if (s.autoRun) {
    char buf[12];
    snprintf(buf, sizeof(buf), "Daily %02d:%02d", s.runHour, s.runMinute);
    gfx.print(buf);
  } else {
    gfx.print("Manual only");
  }

  gfx.setTextColor(C_DIM);
  gfx.setCursor(270, y + 22);
  gfx.print(s.stepCount);
  gfx.print(" zones");

  _btn(SL_RUN_X, y + 9, 120, SL_ROW_H - 18,
       active ? C_RED : C_GREEN,
       active ? "STOP" : "RUN");
}

void _drawSchedulesScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("SCHEDULES");
  int row = 0;
  for (int i = 0; i < MAX_SCHEDULES && row < 5; i++) {
    if (!schedules[i].used) continue;
    _drawSchedRow(row++, i);
  }
  if (row == 0) {
    gfx.setTextColor(C_DIM);
    gfx.setTextSize(2);
    gfx.setCursor(20, HDR_H + 40);
    gfx.print("No schedules yet.");
    gfx.setCursor(20, HDR_H + 70);
    gfx.print("Create one via the API, then tap it here to configure.");
  }
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 160, 50, C_GRAY, "BACK");
  _btn(SCR_W - 250, NAV_Y + 10, 230, 50, C_RED,  "STOP ALL");
  _drawStatusBar();
}

void _handleSchedulesTouch(int tx, int ty) {
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50))          { currentScreen = SCR_HOME; needsRedraw = true; return; }
  if (_inRect(tx, ty, SCR_W - 250, NAV_Y + 10, 230, 50)) {
    // Stop the running schedule; redraw only the row that changed + status bar
    int prevIdx = runState.scheduleIndex;
    stopSchedule();
    int row = 0;
    for (int i = 0; i < MAX_SCHEDULES && row < 5; i++) {
      if (!schedules[i].used) continue;
      if (i == prevIdx) { _drawSchedRow(row, i); break; }
      row++;
    }
    _drawStatusBar();
    return;
  }

  int row = 0;
  for (int i = 0; i < MAX_SCHEDULES && row < 5; i++) {
    if (!schedules[i].used) continue;
    int y = SL_LIST_Y + row * (SL_ROW_H + SL_ROW_GAP);
    // Run/Stop button — redraw just that row + status bar
    if (_inRect(tx, ty, SL_RUN_X, y + 9, 120, SL_ROW_H - 18)) {
      if (runState.running && runState.scheduleIndex == i) stopSchedule();
      else startSchedule(i);
      _drawSchedRow(row, i);
      _drawStatusBar();
      return;
    }
    // Row body → edit screen (full redraw needed for new screen)
    if (_inRect(tx, ty, 10, y, SCR_W - 20, SL_ROW_H)) {
      _loadEditState(i);
      currentScreen = SCR_EDIT;
      needsRedraw = true;
      return;
    }
    row++;
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
       _editZones[z].enabled ? "ON" : "OFF", 1);

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
       _editAutoRun ? "AUTO ON" : "AUTO OFF", 1);

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
  snprintf(title, sizeof(title), "EDIT: %s", schedules[_editIdx].name);
  _drawHeader(title);

  for (int z = 0; z < ZONE_COUNT; z++) _drawEditZoneRow(z);
  _drawEditSettings();

  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20,          NAV_Y + 10, 160, 50, C_GRAY,  "CANCEL");
  _btn(320,         NAV_Y + 10, 160, 50, C_BLUE,  "SAVE");
  _btn(SCR_W - 200, NAV_Y + 10, 180, 50, C_GREEN, "RUN NOW");
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
    startSchedule(_editIdx);
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

// ── Public API ─────────────────────────────────────────────────────────────

void displayInit() {
  gfx.begin();
  gfx.setRotation(3);
  gfx.fillScreen(C_BG);
  touch.begin();
  touch.onDetect(_onTouch);
  needsRedraw = true;
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
  // Full redraw only when changing screens or on first boot
  if (needsRedraw) {
    switch (currentScreen) {
      case SCR_HOME:      _drawHomeScreen();      break;
      case SCR_SCHEDULES: _drawSchedulesScreen(); break;
      case SCR_EDIT:      _drawEditScreen();      break;
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

  // Consume touch event — ignore taps within 350ms of the last one
  static unsigned long _lastTouchMs = 0;
  if (_touched) {
    _touched = false;
    if (millis() - _lastTouchMs > 350) {
      _lastTouchMs = millis();
      int tx = _touchX, ty = _touchY;
      switch (currentScreen) {
        case SCR_HOME:      _handleHomeTouch(tx, ty);      break;
        case SCR_SCHEDULES: _handleSchedulesTouch(tx, ty); break;
        case SCR_EDIT:      _handleEditTouch(tx, ty);      break;
      }
    }
  }
}
