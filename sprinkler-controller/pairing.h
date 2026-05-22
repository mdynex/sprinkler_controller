#pragma once
#include <WiFi.h>
#include <ArduinoJson.h>
#include <qrcode.h>
#include "config.h"
#include "state.h"
#include "storage.h"
#include "http_utils.h"
#include "display.h"

// =============================================================================
// pairing.h — Pairing screen (SCR_PAIRING) and POST /provision handler
//
// While `deviceState == STATE_PAIRING` the display shows a QR + 6-digit code
// derived from `claimSecret`. The server, called from the app, hits
// POST /provision on us with that secret as `X-Claim-Secret` and pushes:
//
//   { "server_host":"...", "device_id":"...", "device_token":"...", "family_id":N }
//
// We persist host/device_id/device_token to KVStore, switch to STATE_RUNNING,
// and the display flips back to SCR_HOME.
//
// QR payload format:
//   homeai-claim://<ip>:<port>?s=<claim_secret>&n=<device_name>
//
// The Android app scans this, POSTs to /devices/claim on the server, and
// the server in turn POSTs /provision to us.
// =============================================================================

// ── QR object (initialised lazily on first draw) ───────────────────────────
static QRCode _qr;
static const uint8_t _QR_VERSION = 5;     // 37×37 modules, ~106 bytes capacity at ECC_LOW

// qrcode_getBufferSize() is a runtime function on this toolchain, so we can't
// use it as a compile-time array bound. Compute the same value via constexpr:
//   side = 4*version + 17     (37 at v=5)
//   bytes = (side*side + 7) / 8   (172 at v=5)
constexpr int _qrSideForVersion(int v)  { return 4 * v + 17; }
constexpr int _qrBufBytesForVersion(int v) {
    return (_qrSideForVersion(v) * _qrSideForVersion(v) + 7) / 8;
}
static uint8_t _qrBuf[_qrBufBytesForVersion(_QR_VERSION)];
static bool _qrReady = false;

// Replace spaces with '+' so the QR payload stays URL-safe without %-encoding.
static String _urlEncodeSpaces(const String& s) {
  String out = s;
  out.replace(' ', '+');
  return out;
}

static void _initPairingQR() {
  String ip   = WiFi.localIP().toString();
  String name = _urlEncodeSpaces(String(DEVICE_NAME));
  String url  = "homeai-claim://" + ip + ":80?s=" + String(claimSecret) + "&n=" + name;
  qrcode_initText(&_qr, _qrBuf, _QR_VERSION, ECC_LOW, url.c_str());
  _qrReady = true;
}

// Public: invalidate the cached QR so the next paint re-encodes the current
// claimSecret. Call this whenever generateClaimSecret() runs (e.g. Re-pair).
void invalidatePairingQR() { _qrReady = false; }

// ── Screen drawing ──────────────────────────────────────────────────────────

void _drawPairingScreen() {
  gfx.fillScreen(C_BG);
  _drawHeader("PAIR THIS CONTROLLER");

  // Device name + IP at the top
  gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
  gfx.setCursor(20, HDR_H + 16);
  gfx.print(DEVICE_NAME);

  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(20, HDR_H + 42);
  gfx.print("IP: ");
  gfx.print(WiFi.localIP());

  // QR code — render in a white box (quiet zone)
  if (!_qrReady) _initPairingQR();
  const int moduleSize = 7;                          // 37 × 7 = 259 px
  const int qrW = _qr.size * moduleSize;
  const int qrX = (SCR_W - qrW) / 2;
  const int qrY = HDR_H + 70;
  gfx.fillRect(qrX - 10, qrY - 10, qrW + 20, qrW + 20, C_WHITE);
  for (uint8_t row = 0; row < _qr.size; row++) {
    for (uint8_t col = 0; col < _qr.size; col++) {
      if (qrcode_getModule(&_qr, col, row)) {
        gfx.fillRect(qrX + col * moduleSize, qrY + row * moduleSize,
                     moduleSize, moduleSize, C_BLACK);
      }
    }
  }

  // 6-digit numeric code, spaced for readability ("4 1 7 0 5 2")
  String spaced;
  for (int i = 0; i < 6; i++) {
    if (i > 0) spaced += "  ";
    spaced += claimCode[i];
  }
  gfx.setTextColor(C_WHITE); gfx.setTextSize(4);
  int codeW = spaced.length() * 24;
  gfx.setCursor((SCR_W - codeW) / 2, qrY + qrW + 22);
  gfx.print(spaced);

  // Instruction line
  const char* line1 = "Open the app -> Add Device -> Scan QR or enter code";
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  int t1w = strlen(line1) * 6;
  gfx.setCursor((SCR_W - t1w) / 2, qrY + qrW + 60);
  gfx.print(line1);

  // Skip button — local-only mode without server pairing
  gfx.fillRect(0, NAV_Y, SCR_W, NAV_H, C_BG);
  _btn(SCR_W - 260, NAV_Y + 10, 240, 50, C_GRAY, "SKIP FOR NOW", 2);

  // Bottom status line
  gfx.fillRect(0, STS_Y, SCR_W, STS_H, C_BLACK);
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(10, STS_Y + 14);
  gfx.print("Waiting for app...  (or skip to use without an app)");
}

void _handlePairingTouch(int tx, int ty) {
  // Skip → standalone mode. Persist the choice so we don't show the pairing
  // screen again on the next reboot. Settings → Re-pair clears the flag.
  if (_inRect(tx, ty, SCR_W - 260, NAV_Y + 10, 240, 50)) {
    storage::savePairSkipped();
    deviceState   = STATE_RUNNING;
    currentScreen = SCR_HOME;
    needsRedraw   = true;
    Serial.println("[pairing] Skipped — standalone mode");
  }
}

// ── POST /provision handler ─────────────────────────────────────────────────

void routeProvision(WiFiClient& client, HttpRequest& req) {
  if (deviceState != STATE_PAIRING) {
    sendResponse(client, 409, "{\"error\":\"already paired\"}");
    return;
  }
  // Accept either form: QR encodes the full 16-hex `claimSecret`,
  // manual entry uses the 6-digit `claimCode`.
  Serial.print("[provision] Received secret: '");
  Serial.print(req.claimSecret);
  Serial.print("' (len=");
  Serial.print(req.claimSecret.length());
  Serial.print(")  Expected secret: '");
  Serial.print(claimSecret);
  Serial.print("' code: '");
  Serial.print(claimCode);
  Serial.println("'");

  if (req.claimSecret != String(claimSecret) &&
      req.claimSecret != String(claimCode)) {
    sendResponse(client, 401, "{\"error\":\"invalid claim secret\"}");
    return;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
    sendResponse(client, 400, "{\"error\":\"bad json\"}");
    return;
  }

  const char* host  = doc["server_host"]  | "";
  const char* did   = doc["device_id"]    | "";
  const char* token = doc["device_token"] | "";
  if (!*host || !*did || !*token) {
    sendResponse(client, 400, "{\"error\":\"missing field\"}");
    return;
  }

  if (!storage::saveServerConfig(host, did, token)) {
    sendResponse(client, 500, "{\"error\":\"storage write failed\"}");
    return;
  }

  // Copy into globals
  strncpy(serverHost,  host,  sizeof(serverHost)  - 1); serverHost[sizeof(serverHost)   - 1] = '\0';
  strncpy(deviceId,    did,   sizeof(deviceId)    - 1); deviceId[sizeof(deviceId)       - 1] = '\0';
  strncpy(deviceToken, token, sizeof(deviceToken) - 1); deviceToken[sizeof(deviceToken) - 1] = '\0';

  Serial.print("[provision] Paired with server ");
  Serial.println(serverHost);

  deviceState   = STATE_RUNNING;
  currentScreen = SCR_HOME;
  needsRedraw   = true;

  sendResponse(client, 200, "{\"ok\":true}");
}
