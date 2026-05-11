#pragma once
#include <WiFi.h>

// =============================================================================
// http_utils.h — Minimal HTTP/1.1 request parser and response sender
//
// The Arduino WiFiServer gives us raw TCP connections. This file handles the
// HTTP layer: reading the request line and headers from the client, reading
// any POST/PUT body, and writing a well-formed HTTP response back.
//
// Only the fields the API actually needs are extracted:
//   method  — "GET", "POST", "PUT", or "DELETE"
//   path    — the URL path, e.g. "/zones/1/on"
//   body    — raw request body (for POST/PUT JSON payloads)
// =============================================================================

struct HttpRequest {
  String method;
  String path;
  String body;
  int    contentLength;
};

// Read a full HTTP request from the connected client.
// Times out after 2 seconds to avoid hanging on slow or dropped connections.
HttpRequest readRequest(WiFiClient& client) {
  HttpRequest req;
  req.contentLength = 0;

  String line = "";
  bool firstLine = true;
  unsigned long deadline = millis() + 2000;

  while (client.connected() && millis() < deadline) {
    if (!client.available()) continue;
    char c = client.read();

    if (c == '\n') {
      if (firstLine) {
        // First line is "METHOD /path HTTP/1.1" — parse method and path
        int s1 = line.indexOf(' ');
        int s2 = line.indexOf(' ', s1 + 1);
        if (s1 > 0 && s2 > s1) {
          req.method = line.substring(0, s1);
          req.path   = line.substring(s1 + 1, s2);
        }
        firstLine = false;
      } else {
        // Subsequent lines are headers — look for Content-Length
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:"))
          req.contentLength = line.substring(line.indexOf(':') + 1).toInt();
        // An empty line marks the end of headers
        if (line == "" || line == "\r") break;
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }

  // Read the request body if Content-Length was set (POST/PUT with JSON)
  if (req.contentLength > 0) {
    deadline = millis() + 1500;
    while ((int)req.body.length() < req.contentLength && millis() < deadline) {
      if (client.available()) req.body += (char)client.read();
    }
  }

  return req;
}

// Send an HTTP/1.1 JSON response back to the client.
void sendResponse(WiFiClient& client, int status, const String& body) {
  const char* text = (status == 200) ? "OK" :
                     (status == 201) ? "Created" :
                     (status == 400) ? "Bad Request" :
                     (status == 404) ? "Not Found" : "Internal Server Error";

  client.print("HTTP/1.1 ");
  client.print(status);
  client.print(" ");
  client.println(text);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println("Connection: close");
  client.println();   // blank line separates headers from body
  client.print(body);
}
