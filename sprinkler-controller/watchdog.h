#pragma once
#include "mbed.h"

// =============================================================================
// watchdog.h — Hardware watchdog (STM32H747 independent watchdog / IWDG)
//
// The "run forever" insurance. loop() is a single cooperative thread: touch,
// the HTTP server, NTP, the scheduler and the heartbeat are all serviced from
// it. If loop() ever stops turning — a wedged WiFi-module syscall that never
// returns, a heap-exhaustion fault, an I2C stall on the touch controller — no
// software recovery can run, because the recovery code lives inside loop()
// too. The IWDG runs in hardware, independent of the CPU: if it isn't kicked
// within WATCHDOG_TIMEOUT_MS it resets the board.
//
// A reset is fail-safe here: zonesInit() drives every relay pin HIGH (off) at
// boot, so no valve can stick open across a reboot. (Schedules live in RAM and
// are lost on reset — an in-progress watering run is interrupted, which is the
// right trade vs. a frozen controller that can't be stopped at all.)
//
// Kick points:
//   • top of loop()                            — the normal path
//   • inside wifi_setup::run()'s blocking loop  — that wizard legitimately
//     blocks waiting for a human, so it must keep the dog fed itself
//
// Intentional bounded blocking elsewhere (NTP wait 2s, silent reconnect ~8s,
// heartbeat response wait 3s) all finish well inside the timeout, so they
// never trip it. Only an *un*intended hang longer than the timeout resets.
// =============================================================================

namespace wdt {

  // Must exceed the longest intended single-iteration blocking window
  // (silent reconnect ≈ 8s) with comfortable margin, while still recovering
  // from a true hang quickly. 30s is within the STM32H747 IWDG maximum
  // (~32.7s); start() returns false if the platform rejects it.
  static const uint32_t WATCHDOG_TIMEOUT_MS = 30000;

  // Arm the watchdog. Call once, at the very end of setup(), AFTER the long
  // boot-time blocking (WiFi connect, on-screen setup, NTP) is finished —
  // those run unprotected on purpose.
  inline void begin() {
    if (mbed::Watchdog::get_instance().start(WATCHDOG_TIMEOUT_MS)) {
      Serial.print("[wdt] Hardware watchdog armed, timeout ");
      Serial.print(WATCHDOG_TIMEOUT_MS);
      Serial.println(" ms");
    } else {
      Serial.println("[wdt] WARNING: Watchdog.start() failed — running UNPROTECTED");
    }
  }

  // Feed the dog. Cheap; safe to call as often as you like.
  inline void kick() {
    mbed::Watchdog::get_instance().kick();
  }

}  // namespace wdt
