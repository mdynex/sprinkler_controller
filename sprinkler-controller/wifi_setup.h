#pragma once
#include <WiFi.h>
#include "config.h"
#include "watchdog.h"
#include "state.h"
#include "storage.h"
#include "display.h"

// =============================================================================
// wifi_setup.h — On-screen WiFi provisioning
//
// Three screens:
//   SCR_WIFI_LIST       — list of nearby networks, tap to choose
//   SCR_WIFI_PASSWORD   — QWERTY keyboard for the WPA passphrase
//   SCR_WIFI_CONNECTING — splash shown during the (blocking) connect attempt
//
// Entry point is wifi_setup::run() which blocks until a successful connection.
// Internally it runs its own event loop calling displayLoop(), which dispatches
// drawing + touch to the functions in this namespace via the switch in display.h.
//
// On success, the SSID + password are persisted via storage::saveWifiCreds() and
// the run() function returns. On failure the user is sent back to the list.
// =============================================================================

namespace wifi_setup {

// ── Scan results ──────────────────────────────────────────────────────────
constexpr int MAX_NETWORKS = 12;
static String _ssids[MAX_NETWORKS];
static int    _rssi[MAX_NETWORKS];
static bool   _secured[MAX_NETWORKS];
static int    _netCount = 0;

// ── Selection / password state ────────────────────────────────────────────
static String _selectedSsid    = "";
static bool   _selectedSecured = false;
static char   _passwordBuf[65] = "";
static int    _passwordLen     = 0;
static String _lastError       = "";   // shown briefly on the list after a fail

// ── Keyboard state ────────────────────────────────────────────────────────
enum KbLayout { LAY_ABC, LAY_NUM };
static KbLayout _layout  = LAY_ABC;
static bool     _shifted = false;

// ── Completion / entry flags ──────────────────────────────────────────────
static Screen _returnScreen     = SCR_HOME;   // where to go after success/cancel
static bool   _cancelable       = false;      // show CANCEL button in list
static bool   _connectInFlight  = false;      // guard against re-entrant connect

// ── Keyboard geometry ─────────────────────────────────────────────────────
// Layout budget (800×480 screen):
//   header        ...50
//   SSID label    50..70
//   password box  70..150       (height 80, fillRect clears 60..160)
//   keyboard     160..360       (4 rows × 50 px + 3 gaps × 4 = 212 px)
//   nav bar      370..440
//   status bar   440..480
constexpr int KB_W      = 75;
constexpr int KB_H      = 50;
constexpr int KB_GAP    = 4;
constexpr int KB_Y_BASE = 160;   // first row y, just below the password field

// Layout per row
static const char* _row1Lower = "qwertyuiop";
static const char* _row1Upper = "QWERTYUIOP";
static const char* _row2Lower = "asdfghjkl";
static const char* _row2Upper = "ASDFGHJKL";
static const char* _row3Lower = "zxcvbnm";
static const char* _row3Upper = "ZXCVBNM";
static const char* _row1Num   = "1234567890";
static const char* _row2Num   = "@#$%&*-+()";
static const char* _row3Num   = ".,?!'\":;";

static inline const char* _row1() { return _layout == LAY_NUM ? _row1Num : (_shifted ? _row1Upper : _row1Lower); }
static inline const char* _row2() { return _layout == LAY_NUM ? _row2Num : (_shifted ? _row2Upper : _row2Lower); }
static inline const char* _row3() { return _layout == LAY_NUM ? _row3Num : (_shifted ? _row3Upper : _row3Lower); }

// ── Public API ────────────────────────────────────────────────────────────
void run();                  // Blocking: used at boot when WiFi must come up
void enterFromSettings();    // Non-blocking: starts the wizard, Cancel returns to Settings
void tick();                 // Call every loop() — performs the connect attempt
                             // after the CONNECTING screen has rendered.
void drawListScreen();
void drawPasswordScreen();
void drawConnectingScreen();
void handleListTouch(int tx, int ty);
void handlePasswordTouch(int tx, int ty);

// True while we're inside any wifi-setup screen — the WiFi watchdog uses
// this to avoid recursive trigger.
inline bool isActive() {
  return currentScreen == SCR_WIFI_LIST
      || currentScreen == SCR_WIFI_PASSWORD
      || currentScreen == SCR_WIFI_CONNECTING;
}

// ── Network scan ──────────────────────────────────────────────────────────
static void _doScan() {
  displayStatus("Scanning networks...", "");
  int n = WiFi.scanNetworks();
  if (n < 0) n = 0;
  if (n > MAX_NETWORKS) n = MAX_NETWORKS;
  _netCount = n;
  for (int i = 0; i < n; i++) {
    _ssids[i]   = WiFi.SSID(i);
    _rssi[i]    = WiFi.RSSI(i);
    // Treat any non-open encryption as "secured" so we prompt for a password
    _secured[i] = (WiFi.encryptionType(i) != 7 /* ENC_TYPE_NONE on most cores */);
  }
  // Sort by signal strength descending — simple selection sort
  for (int i = 0; i < n; i++) {
    int best = i;
    for (int j = i + 1; j < n; j++) if (_rssi[j] > _rssi[best]) best = j;
    if (best != i) {
      int   tr = _rssi[i];   _rssi[i]    = _rssi[best];   _rssi[best]    = tr;
      String ts = _ssids[i]; _ssids[i]   = _ssids[best];  _ssids[best]   = ts;
      bool  tb = _secured[i]; _secured[i] = _secured[best]; _secured[best] = tb;
    }
  }
}

// ── List screen ───────────────────────────────────────────────────────────
void drawListScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("CHOOSE WIFI");

  if (_lastError.length() > 0) {
    gfx.fillRect(0, HDR_H, SCR_W, 25, C_RED);
    gfx.setTextColor(C_WHITE); gfx.setTextSize(1);
    gfx.setCursor(10, HDR_H + 8);
    gfx.print(_lastError);
  }

  int yOffset = (_lastError.length() > 0) ? 30 : 5;

  if (_netCount == 0) {
    gfx.setTextColor(C_DIM); gfx.setTextSize(2);
    gfx.setCursor(20, HDR_H + yOffset + 30);
    gfx.print("No networks found.");
    gfx.setCursor(20, HDR_H + yOffset + 60);
    gfx.print("Tap RESCAN below.");
  } else {
    const int rowH = 50;
    int maxRows = (NAV_Y - HDR_H - yOffset - 10) / (rowH + 4);
    int shown = (_netCount < maxRows) ? _netCount : maxRows;
    for (int i = 0; i < shown; i++) {
      int y = HDR_H + yOffset + i * (rowH + 4);
      gfx.fillRoundRect(15, y, SCR_W - 30, rowH, 6, C_DARKGRAY);

      gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
      gfx.setCursor(25, y + 10);
      gfx.print(_ssids[i]);

      if (_secured[i]) {
        gfx.setTextColor(C_DIM); gfx.setTextSize(1);
        gfx.setCursor(25, y + 32);
        gfx.print("Secured");
      }

      // Signal bars (1-4) from RSSI
      int bars = 1;
      if (_rssi[i] > -80) bars = 2;
      if (_rssi[i] > -70) bars = 3;
      if (_rssi[i] > -55) bars = 4;
      for (int b = 0; b < 4; b++) {
        int bx = SCR_W - 80 + b * 14;
        int bh = 6 + b * 4;
        gfx.fillRect(bx, y + rowH - 6 - bh, 10, bh, b < bars ? C_GREEN : C_GRAY);
      }
    }
  }

  // Nav: optional CANCEL (only when entered from Settings) + RESCAN
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  if (_cancelable) {
    _btn(20, NAV_Y + 10, 160, 50, C_GRAY, "CANCEL");
  }
  _btn(SCR_W - 220, NAV_Y + 10, 200, 50, C_GRAY, "RESCAN");

  // Status bar
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(10, STS_Y + 14);
  gfx.print("Tap a network to connect");
}

void handleListTouch(int tx, int ty) {
  // Clear any pending error on first touch
  if (_lastError.length() > 0) {
    _lastError = "";
    needsRedraw = true;
    return;
  }

  // CANCEL (Settings entry only)
  if (_cancelable && _inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    currentScreen = _returnScreen;
    needsRedraw   = true;
    return;
  }

  // RESCAN button
  if (_inRect(tx, ty, SCR_W - 220, NAV_Y + 10, 200, 50)) {
    _doScan();
    needsRedraw = true;
    return;
  }

  // Network rows
  const int rowH = 50;
  int maxRows = (NAV_Y - HDR_H - 15) / (rowH + 4);
  int shown = (_netCount < maxRows) ? _netCount : maxRows;
  for (int i = 0; i < shown; i++) {
    int y = HDR_H + 5 + i * (rowH + 4);
    if (_inRect(tx, ty, 15, y, SCR_W - 30, rowH)) {
      _selectedSsid    = _ssids[i];
      _selectedSecured = _secured[i];
      _passwordBuf[0]  = '\0';
      _passwordLen     = 0;
      if (_selectedSecured) {
        _layout = LAY_ABC; _shifted = false;
        currentScreen = SCR_WIFI_PASSWORD;
      } else {
        currentScreen = SCR_WIFI_CONNECTING;
      }
      needsRedraw = true;
      return;
    }
  }
}

// ── Password screen ───────────────────────────────────────────────────────

static void _drawPasswordField() {
  // Clear the whole area above the keyboard so partial repaints don't leave
  // stale pixels behind.
  gfx.fillRect(0, HDR_H, SCR_W, KB_Y_BASE - HDR_H, C_BG);

  // SSID label (y = 55, just below the header)
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, HDR_H + 5);
  gfx.print("Connecting to:");
  gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
  gfx.setCursor(20, HDR_H + 22);
  gfx.print(_selectedSsid);

  // Password input box (y = 95, height 45, clears y = 95..140)
  int boxY = HDR_H + 45;
  gfx.fillRoundRect(20, boxY, SCR_W - 40, 45, 6, C_DARKGRAY);

  String shown;
  for (int i = 0; i < _passwordLen; i++) shown += '*';
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  gfx.setCursor(35, boxY + 11);
  gfx.print(shown);
}

static void _drawKey(int col, int row, int spanCols, const char* lbl,
                     uint16_t color = C_GRAY, int textSize = 2) {
  int x = col * (KB_W + KB_GAP);
  int y = KB_Y_BASE + row * (KB_H + KB_GAP);
  int w = spanCols * KB_W + (spanCols - 1) * KB_GAP;
  _btn(x, y, w, KB_H, color, lbl, textSize);
}

static void _drawKeyboard() {
  // Clear keyboard area
  gfx.fillRect(0, KB_Y_BASE, SCR_W, 4 * (KB_H + KB_GAP), C_BG);

  // Row 0: 10 keys
  const char* r1 = _row1();
  for (int i = 0; i < (int)strlen(r1); i++) {
    char buf[2] = { r1[i], 0 };
    _drawKey(i, 0, 1, buf);
  }
  // Row 1: 9 keys (left-aligned for simplicity)
  const char* r2 = _row2();
  for (int i = 0; i < (int)strlen(r2); i++) {
    char buf[2] = { r2[i], 0 };
    _drawKey(i, 1, 1, buf);
  }
  // Row 2: shift, 7 letters, back
  _drawKey(0, 2, 1, _shifted ? "SH" : "sh", _shifted ? C_BLUE : C_GRAY, 1);
  const char* r3 = _row3();
  int nR3 = strlen(r3);
  for (int i = 0; i < nR3; i++) {
    char buf[2] = { r3[i], 0 };
    _drawKey(i + 1, 2, 1, buf);
  }
  _drawKey(nR3 + 1, 2, 1, "<X", C_GRAY, 1);  // backspace

  // Row 3: 123/ABC, space, cancel, enter
  _drawKey(0, 3, 2, _layout == LAY_NUM ? "ABC" : "123", C_GRAY, 1);
  _drawKey(2, 3, 5, "_____", C_DARKGRAY, 1);   // space (visual cue)
  _drawKey(7, 3, 1, "CXL", C_RED, 1);
  _drawKey(8, 3, 2, "GO", C_GREEN, 2);
}

void drawPasswordScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("WIFI PASSWORD");
  _drawPasswordField();
  _drawKeyboard();
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
}

void handlePasswordTouch(int tx, int ty) {
  // What row was touched?
  int rowH = KB_H + KB_GAP;
  if (ty < KB_Y_BASE || ty >= KB_Y_BASE + 4 * rowH) return;
  int row = (ty - KB_Y_BASE) / rowH;
  int col = tx / (KB_W + KB_GAP);
  if (col < 0 || col > 9) return;

  switch (row) {
    case 0: {  // numeric / letters row
      const char* r = _row1();
      if (col < (int)strlen(r) && _passwordLen < 64) {
        _passwordBuf[_passwordLen++] = r[col];
        _passwordBuf[_passwordLen] = '\0';
        _drawPasswordField();
      }
      break;
    }
    case 1: {
      const char* r = _row2();
      if (col < (int)strlen(r) && _passwordLen < 64) {
        _passwordBuf[_passwordLen++] = r[col];
        _passwordBuf[_passwordLen] = '\0';
        _drawPasswordField();
      }
      break;
    }
    case 2: {
      const char* r = _row3();
      int nR = strlen(r);
      if (col == 0) {
        _shifted = !_shifted;
        _drawKeyboard();
      } else if (col >= 1 && col <= nR) {
        if (_passwordLen < 64) {
          _passwordBuf[_passwordLen++] = r[col - 1];
          _passwordBuf[_passwordLen] = '\0';
          _drawPasswordField();
        }
      } else if (col == nR + 1) {
        if (_passwordLen > 0) {
          _passwordBuf[--_passwordLen] = '\0';
          _drawPasswordField();
        }
      }
      break;
    }
    case 3: {
      if (col >= 0 && col < 2) {
        _layout  = (_layout == LAY_NUM) ? LAY_ABC : LAY_NUM;
        _shifted = false;
        _drawKeyboard();
      } else if (col >= 2 && col < 7) {
        if (_passwordLen < 64) {
          _passwordBuf[_passwordLen++] = ' ';
          _passwordBuf[_passwordLen] = '\0';
          _drawPasswordField();
        }
      } else if (col == 7) {
        currentScreen = SCR_WIFI_LIST;
        needsRedraw = true;
      } else if (col >= 8) {
        currentScreen = SCR_WIFI_CONNECTING;
        needsRedraw = true;
      }
      break;
    }
  }
}

// ── Connecting screen ─────────────────────────────────────────────────────

void drawConnectingScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("CONNECTING");
  gfx.setTextColor(C_DIM); gfx.setTextSize(2);
  gfx.setCursor(20, HDR_H + 60);
  gfx.print("Connecting to:");
  gfx.setTextColor(C_WHITE); gfx.setTextSize(3);
  gfx.setCursor(20, HDR_H + 100);
  gfx.print(_selectedSsid);
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, HDR_H + 160);
  gfx.print("Please wait — this can take up to 15 seconds.");
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
}

// Attempts the actual connect. Blocking up to 15 s.
static bool _attemptConnect() {
  WiFi.begin(_selectedSsid.c_str(), _passwordBuf);
  unsigned long start = millis();
  while (millis() - start < 15000) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(500);
  }
  WiFi.disconnect();
  return false;
}

// ── Public entry points ───────────────────────────────────────────────────

// Call every iteration of loop(). If the CONNECTING screen has been drawn
// and we have a pending connect attempt, run it (blocking up to 15 s).
// On success: save creds and switch to _returnScreen. On failure: back to
// the list with an error banner.
void tick() {
  if (currentScreen != SCR_WIFI_CONNECTING) return;
  if (needsRedraw)         return;
  if (_connectInFlight)    return;
  _connectInFlight = true;
  bool ok = _attemptConnect();
  _connectInFlight = false;
  if (ok) {
    storage::saveWifiCreds(_selectedSsid.c_str(), _passwordBuf);
    currentScreen = _returnScreen;
    needsRedraw   = true;
  } else {
    _lastError    = "Could not connect — check the password";
    currentScreen = SCR_WIFI_LIST;
    needsRedraw   = true;
  }
}

// Blocking entry — used at boot when there's no usable WiFi config.
// Returns only once the user has successfully connected.
void run() {
  _cancelable    = false;
  _returnScreen  = SCR_HOME;
  _lastError     = "";
  _doScan();
  currentScreen  = SCR_WIFI_LIST;
  needsRedraw    = true;
  // Loop until we leave the wifi-setup screens (set by tick() on success).
  // This blocks for a human, so it must keep feeding the watchdog itself —
  // otherwise an unattended-but-mid-setup board would reset every 30s.
  while (isActive()) {
    wdt::kick();
    displayLoop();
    tick();
  }
}

// Non-blocking entry — used from the Settings menu. Caller stays in its own
// loop; the user can navigate the wifi setup and tap CANCEL to return to
// _returnScreen without changing creds.
void enterFromSettings() {
  _cancelable   = true;
  _returnScreen = SCR_SETTINGS;
  _lastError    = "";
  _doScan();
  currentScreen = SCR_WIFI_LIST;
  needsRedraw   = true;
}

}  // namespace wifi_setup
