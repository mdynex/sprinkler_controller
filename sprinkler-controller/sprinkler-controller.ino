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
//   1. displayInit / zonesInit / jobsInit, then reload saved jobs from flash
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
#include "watchdog.h"
#include "state.h"
#include "http_utils.h"
#include "zones.h"
#include "ntp.h"
#include "jobs.h"
#include "storage.h"   // includes jobs.h — storage knows Job, never the reverse
#include "missed.h"
#include "display.h"
#include "wifi_setup.h"
#include "settings.h"
#include "pairing.h"
#include "api.h"
#include "heartbeat.h"
#include "wifi_recovery.h"

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
  jobsInit();

  // Jobs survive power cycles. Load them before anything can run, and give
  // jobs.h a way to write back when the runner retires or disables one.
  storage::loadAllJobs();
  onJobPersistNeeded = []() { storage::saveAllJobs(); };
  Serial.print("[boot] Loaded ");
  Serial.print(jobCount());
  Serial.println(" job(s) from flash");

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

  // Work out what the device slept through while it was powered off. Needs a
  // trustworthy clock, so it runs after NTP — and is retried from loop() if
  // this first sync failed.
  missed::scanAtBoot();

  // 2. Boot routing:
  //    - Server config present → STATE_RUNNING (paired)
  //    - User skipped pairing earlier → STATE_RUNNING (standalone, no server)
  //    - Otherwise → STATE_PAIRING (show QR + claim code)
  if (storage::loadServerConfig(serverHost, sizeof(serverHost),
                                deviceId,   sizeof(deviceId),
                                deviceToken, sizeof(deviceToken))) {
    deviceState   = STATE_RUNNING;
    currentScreen = SCR_HOME;
    Serial.print("[boot] Paired with server ");
    Serial.println(serverHost);
  } else if (storage::loadPairSkipped()) {
    deviceState   = STATE_RUNNING;
    currentScreen = SCR_HOME;
    Serial.println("[boot] Standalone mode (pairing skipped)");
  } else {
    generateClaimSecret();
    deviceState   = STATE_PAIRING;
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

  // 4. Arm the hardware watchdog. Done last, after all the long boot-time
  //    blocking (WiFi connect, on-screen setup, NTP) so those don't trip it.
  //    From here on, loop() must kick it within wdt::WATCHDOG_TIMEOUT_MS or
  //    the board resets.
  wdt::begin();
}

// ── Loop ────────────────────────────────────────────────────────────────────

void loop() {
  wdt::kick();          // feed the hardware watchdog — must happen every pass
  heapLogTick();        // diagnostic: log free heap once a minute (see heartbeat.h)

  ntpUpdate();          // re-sync time once per hour
  wifi_setup::tick();   // performs the connect attempt after CONNECTING is drawn

  missed::scanAtBoot();   // no-op once it has run; retries if NTP was late
  missed::tick();         // refresh the liveness marker every few minutes

  if (deviceState == STATE_RUNNING) {
    heartbeatTick();      // send keepalive to server every 60s
    jobTick();            // advance the running job
    jobSchedulerCheck();  // fire any job whose time has come
  }
  displayLoop();        // touch input + redraws

  // WiFi watchdog — silent reconnect tier (wifi_recovery) runs first.
  // If WiFi.status() reports disconnected, or 3 heartbeats in a row miss
  // the server (catches the wedged-stack case where status() lies),
  // wifi_recovery silently disconnects + WiFi.begin()s with backoff. Only
  // when all silent attempts are exhausted do we escalate to the on-screen
  // setup wizard, since that one needs a human to interact with it.
  if (!wifi_setup::isActive()) {
    wifi_recovery::tick();
    if (wifi_recovery::exhausted()) {
      Serial.println("[wifi] Silent recovery exhausted — entering setup wizard");
      wifi_setup::run();         // blocking
      wifi_recovery::reset();
      needsRedraw = true;
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
