#pragma once
#include <WiFi.h>

struct HttpRequest {
  String method;
  String path;
  String body;
  int    contentLength;
};

// Reads the full HTTP request: first line, headers, and optional body.
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
        // "GET /zones HTTP/1.1"
        int s1 = line.indexOf(' ');
        int s2 = line.indexOf(' ', s1 + 1);
        if (s1 > 0 && s2 > s1) {
          req.method = line.substring(0, s1);
          req.path   = line.substring(s1 + 1, s2);
        }
        firstLine = false;
      } else {
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:"))
          req.contentLength = line.substring(line.indexOf(':') + 1).toInt();
        // Blank line signals end of headers
        if (line == "" || line == "\r") break;
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }

  // Read body (POST/PUT payloads)
  if (req.contentLength > 0) {
    deadline = millis() + 1500;
    while ((int)req.body.length() < req.contentLength && millis() < deadline) {
      if (client.available()) req.body += (char)client.read();
    }
  }

  return req;
}

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
  client.println();
  client.print(body);
}
