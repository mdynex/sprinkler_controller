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
//
// Setup:
//   1. Copy config_example.h to config.h and fill in your WiFi credentials.
//   2. Wire each relay IN1–IN8 pin to the Giga R1 digital pins listed in
//      ZONE_PINS inside config.h.
//   3. Connect VCC and GND on the relay module to 5V and GND on the Giga R1.
//   4. Upload this sketch. The board's IP address is shown on the display
//      and in the Serial Monitor (115200 baud) once WiFi connects.
//
// See README.md for the full API reference.
// =============================================================================

#include <WiFi.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include <Arduino_GigaDisplay_GFX.h>
#include <Arduino_GigaDisplayTouch.h>
#include "config.h"
#include "registration.h"
#include "http_utils.h"
#include "zones.h"
#include "ntp.h"
#include "schedules.h"
#include "api.h"
#include "display.h"

WiFiServer server(80);

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  displayInit();    // start display first so the user sees status messages
  zonesInit();      // configure relay pins and ensure all zones are off
  schedulesInit();  // clear the schedule slots
  connectWiFi();          // block until connected, showing status on screen
  registerWithServer();   // register/update IP in the Home AI Server device registry
  ntpInit();              // sync time from pool.ntp.org
  server.begin();         // start the HTTP server on port 80
  Serial.print("API ready at http://");
  Serial.println(WiFi.localIP());
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
  ntpUpdate();       // re-syncs time once per hour (non-blocking)
  scheduleTick();    // advances the active schedule one step if its time is up
  schedulerCheck();  // fires any auto-run schedule whose time matches now
  displayLoop();     // processes touch input and redraws the screen if needed

  // Handle one incoming API request per loop iteration
  WiFiClient client = server.available();
  if (client) {
    HttpRequest req = readRequest(client);
    if (req.method.length() > 0) handleApi(client, req);
    client.stop();
  }
}

// ── WiFi ───────────────────────────────────────────────────────────────────

// Connects to WiFi, retrying indefinitely until successful.
// Shows progress on the display so the user knows what's happening.
// Each attempt waits up to 10 seconds before retrying.
void connectWiFi() {
  displayStatus("Connecting to WiFi...", WIFI_SSID);
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  int attempt = 0;
  while (true) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (millis() - start < 10000) {
      if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        displayStatus("Connected!", ip.c_str());
        Serial.print("Connected! IP: ");
        Serial.println(ip);
        delay(1000);  // show the IP on screen briefly before the UI loads
        return;
      }
      delay(500);
      Serial.print(".");
    }
    attempt++;
    Serial.print("\nRetrying (attempt ");
    Serial.print(attempt);
    Serial.println(")...");
    char msg[24];
    snprintf(msg, sizeof(msg), "Retry %d...", attempt);
    displayStatus(msg, WIFI_SSID);
    WiFi.disconnect();
    delay(1000);
  }
}
