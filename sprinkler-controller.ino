#include <WiFi.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include <Arduino_GigaDisplay_GFX.h>
#include <Arduino_GigaDisplayTouch.h>
#include "config.h"
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
  displayInit();   // show the screen as early as possible
  zonesInit();
  schedulesInit();
  connectWiFi();
  ntpInit();
  server.begin();
  Serial.print("API ready at http://");
  Serial.println(WiFi.localIP());
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
  ntpUpdate();       // keeps time synced (hits server at most once per hour)
  scheduleTick();    // advances any active schedule without blocking
  schedulerCheck();  // fires auto-run schedules at their set time
  displayLoop();     // handles touch input and redraws when needed

  WiFiClient client = server.available();
  if (client) {
    HttpRequest req = readRequest(client);
    if (req.method.length() > 0) handleApi(client, req);
    client.stop();
  }
}

// ── WiFi ───────────────────────────────────────────────────────────────────

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");
}
