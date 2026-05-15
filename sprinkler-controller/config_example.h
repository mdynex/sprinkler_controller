#pragma once

// =============================================================================
// config_example.h — Configuration template
//
// Copy this file to config.h and fill in your own values.
// config.h is listed in .gitignore so your credentials are never committed.
// =============================================================================

// ── WiFi ───────────────────────────────────────────────────────────────────
// Use your 2.4 GHz network name and password.
// The Arduino Giga R1 WiFi does NOT support 5 GHz or 6 GHz networks.
const char* WIFI_SSID     = "YOUR_2.4GHz_NETWORK_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ── Timezone ───────────────────────────────────────────────────────────────
// Offset from UTC in seconds.
//   UTC-8  Pacific  = -28800
//   UTC-7  Mountain = -25200
//   UTC-6  Central  = -21600
//   UTC-5  Eastern  = -18000
//   UTC+0  London   =      0
//   UTC+1  Berlin   =   3600
const long UTC_OFFSET_SECONDS = -21600;

// ── Zones ──────────────────────────────────────────────────────────────────
// One digital output pin per sprinkler zone.
// These must match how IN1–IN8 on your relay module are wired to the Giga R1.
const int ZONE_COUNT = 8;
const int ZONE_PINS[ZONE_COUNT] = {2, 3, 4, 5, 6, 7, 8, 9};

// ── Schedules ──────────────────────────────────────────────────────────────
const int MAX_SCHEDULES  = 8;
// MAX_ZONE_STEPS must fit the round-robin cycle-and-soak expansion:
//   worst case = ZONE_COUNT × max_cycles_per_zone  (e.g. 8 zones × 8 cycles = 64)
const int MAX_ZONE_STEPS = 64;

// ── Display ─────────────────────────────────────────────────────────────────
// Seconds of inactivity before the screen backlight turns off.
// Set to 0 to disable sleep entirely.
const int SLEEP_TIMEOUT_SEC = 60;

// ── Home AI Server ───────────────────────────────────────────────────────────
// The Arduino registers itself with the server on each boot so the server
// knows its current IP address and can route /controller/* requests to it.
// Set SERVER_HOST to the Tailscale IP (or LAN IP) of your home server.
const char* SERVER_HOST = "YOUR_SERVER_IP";   // e.g. "100.x.x.x" (Tailscale) or "192.168.1.x"
const char* SERVER_API_KEY = "YOUR_API_KEY";  // must match API_KEY in server/.env
const char* DEVICE_NAME = "Sprinkler Controller";
