#pragma once
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"

// =============================================================================
// registration.h — Home AI Server self-registration
//
// On each boot (after WiFi connects), the Arduino calls POST /devices/pair on
// the Home AI Server. The server records the Arduino's current local IP and
// device type so that /controller/* proxy requests are routed correctly.
//
// The server will return 200 whether the device is new or already registered
// (the server deduplicates by name). An admin must approve the device in the
// web panel or Android app before the proxy routes will use its address.
//
// Call registerWithServer() once from setup(), after connectWiFi() returns.
// =============================================================================

void registerWithServer() {
  String ip = WiFi.localIP().toString();

  // Build JSON body: {"name":"Sprinkler Controller","type":"controller","address":"192.168.x.x"}
  StaticJsonDocument<192> doc;
  doc["name"]    = DEVICE_NAME;
  doc["type"]    = "controller";
  doc["address"] = ip;
  String body;
  serializeJson(doc, body);

  // Connect to server on port 8000
  WiFiClient client;
  if (!client.connect(SERVER_HOST, 8000)) {
    Serial.println("[register] Cannot reach server — skipping registration");
    return;
  }

  int len = body.length();
  client.println("POST /devices/pair HTTP/1.1");
  client.print("Host: "); client.println(SERVER_HOST);
  client.print("X-Api-Key: "); client.println(SERVER_API_KEY);
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(len);
  client.println("Connection: close");
  client.println();
  client.print(body);

  // Wait briefly for response
  unsigned long start = millis();
  while (!client.available() && millis() - start < 5000) delay(10);

  String statusLine = client.readStringUntil('\n');
  client.stop();

  Serial.print("[register] Server response: ");
  Serial.println(statusLine);

  if (statusLine.indexOf("200") >= 0 || statusLine.indexOf("201") >= 0) {
    Serial.println("[register] Registered successfully as " + String(DEVICE_NAME) + " @ " + ip);
  } else {
    Serial.println("[register] Registration returned unexpected status — check API key and server");
  }
}
