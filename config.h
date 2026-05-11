#pragma once

// ── WiFi ───────────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// ── Timezone ───────────────────────────────────────────────────────────────
// Offset from UTC in seconds. Examples:
//   UTC-8  Pacific  = -28800
//   UTC-7  Mountain = -25200
//   UTC-6  Central  = -21600
//   UTC-5  Eastern  = -18000
//   UTC+0  London   =      0
//   UTC+1  Berlin   =   3600
const long UTC_OFFSET_SECONDS = -21600;

// ── Zones ──────────────────────────────────────────────────────────────────
// 8-channel relay module — one pin per zone
// Change these to match how your relay board is wired to the Giga R1
const int ZONE_COUNT = 8;
const int ZONE_PINS[ZONE_COUNT] = {2, 3, 4, 5, 6, 7, 8, 9};

// ── Schedules ──────────────────────────────────────────────────────────────
const int MAX_SCHEDULES  = 8;   // max stored schedules
const int MAX_ZONE_STEPS = 8;   // max zone steps per schedule
