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
  String claimSecret;   // value of the X-Claim-Secret header, empty if absent
};

// Read a full HTTP request from the connected client.
// Times out after 2 seconds to avoid hanging on slow or dropped connections.
HttpRequest readRequest(WiFiClient& client) {
  HttpRequest req;
  req.contentLength = 0;

  String line = "";
  bool firstLine = true;
  // Use elapsed-time comparison (millis() - start), not an absolute deadline,
  // so the loop still terminates correctly across the millis() rollover.
  unsigned long start = millis();

  while (client.connected() && millis() - start < 2000) {
    if (!client.available()) { delay(1); continue; }  // yield to the mbed net thread
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
        // Subsequent lines are headers — capture the ones we care about
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
          req.contentLength = line.substring(line.indexOf(':') + 1).toInt();
        } else if (lower.startsWith("x-claim-secret:")) {
          String v = line.substring(line.indexOf(':') + 1);
          v.trim();
          req.claimSecret = v;
        }
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
    req.body.reserve(req.contentLength);   // one allocation instead of growing byte-by-byte
    unsigned long bodyStart = millis();
    while ((int)req.body.length() < req.contentLength && millis() - bodyStart < 1500) {
      if (client.available()) req.body += (char)client.read();
      else                    delay(1);   // yield instead of busy-spinning
    }
  }

  return req;
}

static const char* _statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 503: return "Service Unavailable";
    case 507: return "Insufficient Storage";
    default:  return "Internal Server Error";
  }
}

// Write just the response headers. Use this when the body is produced in
// pieces (see the job list in api.h) so a large response never has to exist
// as a single allocation. `contentLength` must be the exact total.
void sendHeaders(WiFiClient& client, int status, int contentLength) {
  client.print("HTTP/1.1 ");
  client.print(status);
  client.print(" ");
  client.println(_statusText(status));
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(contentLength);
  client.println("Connection: close");
  client.println();   // blank line separates headers from body
}

// Send a response from a plain char buffer — no String, no heap traffic.
void sendResponseRaw(WiFiClient& client, int status, const char* body, int len) {
  sendHeaders(client, status, len);
  client.write((const uint8_t*)body, len);
}

// Send an HTTP/1.1 JSON response back to the client.
void sendResponse(WiFiClient& client, int status, const String& body) {
  sendResponseRaw(client, status, body.c_str(), (int)body.length());
}
