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
  displayStatus("Connecting to WiFi...", WIFI_SSID);
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  int attempt = 0;
  while (true) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (millis() - start < 10000) {  // wait up to 10s per attempt
      if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        displayStatus("Connected!", ip.c_str());
        Serial.print("Connected! IP: ");
        Serial.println(ip);
        delay(1000);
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
