#pragma once
#include <WiFi.h>
#include "config.h"
#include "state.h"
#include "storage.h"
#include "display.h"
#include "wifi_setup.h"

// =============================================================================
// settings.h — Settings menu, About screen, Re-pair action
//
// Reachable from the home screen via the hamburger icon in the header. The
// menu has three rows:
//
//   1. WiFi Setup        — re-enter the on-screen wizard with a CANCEL option
//   2. Re-pair this device — clear stored server config and bounce back to
//                            STATE_PAIRING with a fresh claim secret/QR
//   3. About             — firmware build, IP, server, device ID, status
// =============================================================================

// Defined in sprinkler-controller.ino — regenerates claimSecret + claimCode.
extern void generateClaimSecret();

namespace settings {

constexpr int ROW_H   = 65;
constexpr int ROW_GAP = 10;

// ── Settings list ──────────────────────────────────────────────────────────

static void _drawRow(int i, const char* label) {
  int y = HDR_H + 20 + i * (ROW_H + ROW_GAP);
  gfx.fillRoundRect(20, y, SCR_W - 40, ROW_H, 8, C_DARKGRAY);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
  gfx.setCursor(35, y + (ROW_H - 16) / 2);
  gfx.print(label);
  gfx.setTextColor(C_DIM); gfx.setTextSize(3);
  gfx.setCursor(SCR_W - 55, y + (ROW_H - 24) / 2);
  gfx.print(">");
}

void drawSettingsScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("SETTINGS");

  _drawRow(0, "WiFi Setup");
  _drawRow(1, "Re-pair this device");
  _drawRow(2, "About");

  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20, NAV_Y + 10, 160, 50, C_GRAY, "BACK");
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
}

void handleSettingsTouch(int tx, int ty) {
  // BACK
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    currentScreen = SCR_HOME;
    needsRedraw = true;
    return;
  }

  for (int i = 0; i < 3; i++) {
    int y = HDR_H + 20 + i * (ROW_H + ROW_GAP);
    if (_inRect(tx, ty, 20, y, SCR_W - 40, ROW_H)) {
      switch (i) {
        case 0:   // WiFi Setup — non-blocking, CANCEL returns here
          wifi_setup::enterFromSettings();
          break;

        case 1: { // Re-pair this device
          storage::clearServerConfig();
          serverHost[0]  = '\0';
          deviceId[0]    = '\0';
          deviceToken[0] = '\0';
          generateClaimSecret();
          deviceState   = STATE_PAIRING;
          currentScreen = SCR_PAIRING;
          needsRedraw   = true;
          break;
        }

        case 2:   // About
          currentScreen = SCR_ABOUT;
          needsRedraw = true;
          break;
      }
      return;
    }
  }
}

// ── About screen ───────────────────────────────────────────────────────────

void drawAboutScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("ABOUT");

  struct Row { const char* label; String value; };

  String idShort = String(deviceId);
  if (idShort.length() > 12) idShort = idShort.substring(0, 8) + "…";
  if (idShort.length() == 0) idShort = "(not paired)";

  Row rows[] = {
    {"Firmware",  String(__DATE__) + " " + __TIME__},
    {"Local IP",  WiFi.localIP().toString()},
    {"Server",    serverHost[0] ? String(serverHost) : String("(not paired)")},
    {"Device ID", idShort},
    {"Status",    deviceState == STATE_RUNNING ? String("Paired") : String("Pairing")},
  };

  int y = HDR_H + 25;
  for (auto& r : rows) {
    gfx.setTextColor(C_DIM); gfx.setTextSize(1);
    gfx.setCursor(20, y);
    gfx.print(r.label);

    gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
    gfx.setCursor(20, y + 14);
    gfx.print(r.value);
    y += 50;
  }

  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(20, NAV_Y + 10, 160, 50, C_GRAY, "BACK");
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
}

void handleAboutTouch(int tx, int ty) {
  if (_inRect(tx, ty, 20, NAV_Y + 10, 160, 50)) {
    currentScreen = SCR_SETTINGS;
    needsRedraw = true;
  }
}

}  // namespace settings
