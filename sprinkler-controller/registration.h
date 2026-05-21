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
// On a successful pairing the server returns a JSON body that includes a
// device token. We store it in deviceToken[] so the heartbeat code can use
// X-Device-Token to keep last_seen current in the Devices screen.
//
// Call registerWithServer() once from setup(), after connectWiFi() returns.
// =============================================================================

// Device token assigned by the server at pairing time.
// 64 hex chars + null terminator. Empty until registration succeeds.
char deviceToken[65] = "";

void registerWithServer() {
  String ip = WiFi.localIP().toString();

  // Build JSON body: {"name":"...","type":"controller","address":"192.168.x.x"}
  StaticJsonDocument<192> reqDoc;
  reqDoc["name"]    = DEVICE_NAME;
  reqDoc["type"]    = "controller";
  reqDoc["address"] = ip;
  String body;
  serializeJson(reqDoc, body);

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

  // ── Read response ─────────────────────────────────────────────────────────

  // Wait for first byte
  unsigned long start = millis();
  while (!client.available() && millis() - start < 5000) delay(10);

  // Read status line
  String statusLine = client.readStringUntil('\n');
  Serial.print("[register] Server response: ");
  Serial.println(statusLine);

  bool ok = statusLine.indexOf("200") >= 0 || statusLine.indexOf("201") >= 0;
  if (!ok) {
    Serial.println("[register] Registration returned unexpected status — check API key and server");
    client.stop();
    return;
  }

  // Skip headers (read until blank line)
  while (client.connected()) {
    String hdr = client.readStringUntil('\n');
    hdr.trim();
    if (hdr.length() == 0) break;  // blank line = end of headers
  }

  // Read response body
  String respBody = "";
  unsigned long bodyDeadline = millis() + 3000;
  while (client.connected() && millis() < bodyDeadline) {
    while (client.available()) {
      respBody += (char)client.read();
    }
  }
  client.stop();

  // Parse token from JSON body
  StaticJsonDocument<512> respDoc;
  DeserializationError err = deserializeJson(respDoc, respBody);
  if (err) {
    Serial.println("[register] Could not parse response JSON — token not stored");
    Serial.print("[register] Registered as ");
    Serial.println(DEVICE_NAME);
    return;
  }

  const char* tok = respDoc["token"];
  if (tok && strlen(tok) > 0) {
    strncpy(deviceToken, tok, sizeof(deviceToken) - 1);
    deviceToken[sizeof(deviceToken) - 1] = '\0';
    Serial.println("[register] Device token stored");
  }

  Serial.print("[register] Registered successfully as ");
  Serial.print(DEVICE_NAME);
  Serial.print(" @ ");
  Serial.println(ip);
}
