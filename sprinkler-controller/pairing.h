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

  // Layout target (avoids the nav bar that starts at NAV_Y=370):
  //   y= 55..80   Device name (size 2) + IP (size 1) on one line
  //   y= 85..270  QR code (37 × 5 px modules + 10 px white quiet zone)
  //   y=280..312  6-digit code (text size 4 ≈ 32 px tall)
  //   y=320..350  Instruction line
  //   y=370+      Nav row with SKIP button

  // Device name + IP — single line, compact
  gfx.setTextColor(C_WHITE); gfx.setTextSize(2);
  gfx.setCursor(20, HDR_H + 8);
  gfx.print(DEVICE_NAME);

  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  gfx.setCursor(SCR_W - 220, HDR_H + 14);
  gfx.print(WiFi.localIP());

  // QR code — render in a white box (quiet zone)
  if (!_qrReady) _initPairingQR();
  const int moduleSize = 5;                          // 37 × 5 = 185 px
  const int qrW = _qr.size * moduleSize;
  const int qrX = (SCR_W - qrW) / 2;
  const int qrY = HDR_H + 38;                        // top of QR
  gfx.fillRect(qrX - 8, qrY - 8, qrW + 16, qrW + 16, C_WHITE);
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
  gfx.setCursor((SCR_W - codeW) / 2, qrY + qrW + 15);
  gfx.print(spaced);

  // Instruction line
  const char* line1 = "Open the app -> Add Device -> Scan QR or enter code";
  gfx.setTextColor(C_DIM); gfx.setTextSize(1);
  int t1w = strlen(line1) * 6;
  gfx.setCursor((SCR_W - t1w) / 2, qrY + qrW + 55);
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
//
// The 6-digit code is only 10^6 guesses, so wrong attempts are limited: after
// MAX_CLAIM_FAILURES misses the code and secret are replaced (the screen
// shows the new ones) and /provision answers 429 for CLAIM_LOCKOUT_MS. That
// caps a LAN attacker at a few guesses a minute against a code that keeps
// changing, while someone who mistypes just reads the new code.

extern void generateClaimSecret();   // sprinkler-controller.ino

constexpr int           MAX_CLAIM_FAILURES = 5;
constexpr unsigned long CLAIM_LOCKOUT_MS   = 60000UL;

static int           _claimFailures   = 0;
static bool          _claimLocked     = false;
static unsigned long _claimLockedAtMs = 0;

void routeProvision(WiFiClient& client, HttpRequest& req) {
  if (deviceState != STATE_PAIRING) {
    sendResponse(client, 409, "{\"error\":\"already paired\"}");
    return;
  }
  if (_claimLocked) {
    if (millis() - _claimLockedAtMs < CLAIM_LOCKOUT_MS) {
      sendResponse(client, 429, "{\"error\":\"too many wrong codes, try again in a minute\"}");
      return;
    }
    _claimLocked = false;
  }

  // Accept either form: QR encodes the full 16-hex `claimSecret`,
  // manual entry uses the 6-digit `claimCode`.
  const char* got = req.claimSecret.c_str();
  if (!secretsEqual(got, claimSecret) && !secretsEqual(got, claimCode)) {
    _claimFailures++;
    Serial.print("[provision] Wrong claim code (");
    Serial.print(_claimFailures);
    Serial.print("/");
    Serial.print(MAX_CLAIM_FAILURES);
    Serial.println(")");
    if (_claimFailures >= MAX_CLAIM_FAILURES) {
      Serial.println("[provision] Too many wrong codes - new code, locked for 60s");
      generateClaimSecret();
      invalidatePairingQR();
      needsRedraw      = true;
      _claimFailures   = 0;
      _claimLocked     = true;
      _claimLockedAtMs = millis();
    }
    sendResponse(client, 401, "{\"error\":\"invalid claim secret\"}");
    return;
  }
  _claimFailures = 0;

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
