#pragma once

// =============================================================================
// config_example.h — Configuration template
//
// Copy this file to config.h. config.h is listed in .gitignore.
//
// As of the on-screen provisioning rewrite, WiFi credentials and the Home AI
// Server URL are no longer compile-time settings:
//   • WiFi  — picked from the on-screen network list on first boot and saved
//             to KVStore.
//   • Server — pushed to the Arduino via POST /provision when a user claims
//              the device from the Android app, then saved to KVStore.
//
// The only compile-time settings that remain are the hardware-specific knobs
// below.
// =============================================================================

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

// ── Display ─────────────────────────────────────────────────────────────────
// Seconds of inactivity before the screen backlight turns off.
// Set to 0 to disable sleep entirely.
const int SLEEP_TIMEOUT_SEC = 60;

// ── Device name ─────────────────────────────────────────────────────────────
// Friendly default shown on the pairing screen and pre-filled in the app's
// Add Device sheet. The user can rename the device when they claim it.
const char* DEVICE_NAME = "Sprinkler Controller";

// ── Safety ──────────────────────────────────────────────────────────────────
// Optional. Longest a zone may stay open when it was turned on by hand (home
// screen tap, POST /zones/{id}/on, or a /command zone_on with no duration).
// Scheduled job steps are not affected. Default is 1 hour (see jobs.h).
// #define MANUAL_ZONE_MAX_SECONDS (60UL * 60UL)
