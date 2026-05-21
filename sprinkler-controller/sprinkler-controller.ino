// =============================================================================
// sprinkler-controller.ino — Main entry point
//
// Hardware:
//   Arduino Giga R1 WiFi        — main microcontroller
//   Arduino Giga Display Shield — 800×480 touchscreen
//   5V 8-Channel Relay Module   — controls sprinkler valves (LOW-TRIGGER)
//
// Required libraries (install via Sketch → Manage Libraries):
//   - ArduinoJson         by Benoit Blanchon
//   - Arduino_GigaDisplay_GFX
//   - Arduino_GigaDisplayTouch
//   - QRCode              by Richard Moore
//
// Boot flow:
//   1. displayInit / zonesInit / schedulesInit (always)
//   2. WiFi: try stored creds → fall back to hardcoded ones in config.h
//      → final fallback enters the on-screen setup wizard (step 7)
//   3. NTP sync
//   4. Server config: if stored in KVStore, go to STATE_RUNNING. Otherwise
//      generate a one-shot claim secret + 6-digit code and enter STATE_PAIRING,
//      waiting for the server to POST /provision to us.
//   5. Start the local HTTP server on port 80.
//
// In STATE_PAIRING only POST /provision is accepted. In STATE_RUNNING
// /zones/*, /schedules/*, /time, etc. are all live and heartbeats flow.
// =============================================================================

#include <WiFi.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include <Arduino_GigaDisplay_GFX.h>
#include <Arduino_GigaDisplayTouch.h>
#include "config.h"
#include "state.h"
#include "storage.h"
#include "http_utils.h"
#include "zones.h"
#include "ntp.h"
#include "schedules.h"
#include "display.h"
#include "wifi_setup.h"
#include "settings.h"
#include "pairing.h"
#include "api.h"
#include "heartbeat.h"

WiFiServer server(80);

// ── Device state — definitions for the externs declared in state.h ─────────
DeviceState deviceState = STATE_WIFI_SETUP;
char serverHost[64]  = "";
char deviceId[37]    = "";
char deviceToken[65] = "";
char claimSecret[17] = "";
char claimCode[7]    = "";

// ── WiFi helpers ────────────────────────────────────────────────────────────

// Attempt to connect to the given network within timeoutMs. Returns true
// on success. Disconnects on failure so the next attempt is clean.
bool connectWifiWithTimeout(const char* ssid, const char* pw, unsigned long timeoutMs) {
  displayStatus("Connecting to WiFi...", ssid);
  Serial.print("[wifi] Connecting to: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pw);
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      displayStatus("Connected!", ip.c_str());
      Serial.print("[wifi] IP: ");
      Serial.println(ip);
      delay(800);
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  WiFi.disconnect();
  return false;
}

// ── Claim secret ────────────────────────────────────────────────────────────

// Generate a 16-hex-character random secret in `claimSecret` and a
// matching 6-digit decimal `claimCode` derived from the first 24 bits.
// The secret authenticates the server when it later POSTs /provision.
void generateClaimSecret() {
  static const char hex[] = "0123456789abcdef";
  randomSeed(analogRead(A0) ^ (uint32_t)millis());
  for (int i = 0; i < 16; i++) claimSecret[i] = hex[random(16)];
  claimSecret[16] = '\0';

  uint32_t v = 0;
  for (int i = 0; i < 6; i++) {
    char c = claimSecret[i];
    int n = (c >= 'a') ? (c - 'a' + 10) : (c - '0');
    v = (v << 4) | (uint32_t)n;
  }
  snprintf(claimCode, sizeof(claimCode), "%06lu", (unsigned long)(v % 1000000UL));
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  displayInit();
  zonesInit();
  schedulesInit();

  // 1. WiFi — try saved creds first
  char ssid[33] = "", pw[65] = "";
  bool connected = false;
  if (storage::loadWifiCreds(ssid, sizeof(ssid), pw, sizeof(pw))) {
    Serial.println("[boot] Found saved WiFi creds — connecting");
    connected = connectWifiWithTimeout(ssid, pw, 15000);
  }
  if (!connected) {
    wifi_setup::run();   // blocks until the user successfully connects
  }

  ntpInit();

  // 2. Choose between RUNNING (already provisioned) and PAIRING (fresh device)
  if (storage::loadServerConfig(serverHost, sizeof(serverHost),
                                deviceId,   sizeof(deviceId),
                                deviceToken, sizeof(deviceToken))) {
    deviceState  = STATE_RUNNING;
    currentScreen = SCR_HOME;
    Serial.print("[boot] Paired with server ");
    Serial.println(serverHost);
  } else {
    generateClaimSecret();
    deviceState  = STATE_PAIRING;
    currentScreen = SCR_PAIRING;
    Serial.print("[boot] Unpaired — claim code: ");
    Serial.print(claimCode);
    Serial.print("  secret: ");
    Serial.println(claimSecret);
  }
  needsRedraw = true;

  // 3. Start the HTTP listener. Route gating by `deviceState` happens in api.h.
  server.begin();
  Serial.print("API ready at http://");
  Serial.println(WiFi.localIP());
}

// ── Loop ────────────────────────────────────────────────────────────────────

// Tracks how long WiFi has been down at runtime; after 60 s we drop into the
// on-screen setup wizard so the user can pick a new network.
static unsigned long _wifiBadSince = 0;

void loop() {
  ntpUpdate();          // re-sync time once per hour
  wifi_setup::tick();   // performs the connect attempt after CONNECTING is drawn

  if (deviceState == STATE_RUNNING) {
    heartbeatTick();    // send keepalive to server every 60s
    scheduleTick();     // advance the active schedule
    schedulerCheck();   // fire any auto-run schedule whose time matches now
  }
  displayLoop();        // touch input + redraws

  // WiFi watchdog — if we're disconnected at runtime for more than 60 s,
  // force the on-screen setup wizard. Skipped while already in setup.
  if (!wifi_setup::isActive()) {
    if (WiFi.status() != WL_CONNECTED) {
      if (_wifiBadSince == 0) _wifiBadSince = millis();
      else if (millis() - _wifiBadSince > 60000UL) {
        Serial.println("[wifi] Disconnected >60s — entering setup wizard");
        wifi_setup::run();        // blocking
        _wifiBadSince = 0;
        needsRedraw = true;
      }
    } else {
      _wifiBadSince = 0;
    }
  }

  // Handle one incoming API request per iteration
  WiFiClient client = server.available();
  if (client) {
    HttpRequest req = readRequest(client);
    if (req.method.length() > 0) handleApi(client, req);
    client.stop();
  }
}
