// ============================================================================
// Storage — local high score & stats in NVS (Preferences)
//
// Persists across reboots AND deep sleep. Wi-Fi credentials are NOT handled
// here — WiFiManager/provisioning (Faza 4) keeps them in its own namespace.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "types.h"

class Storage {
public:
  void begin();

  // Records a finished attempt: bumps the counter and persists new bests.
  // Returns true if a new best volume was set (-> "NEW HIGH SCORE!").
  bool recordAttempt(const AttemptResult &r);

  float    bestVolumeMl()  const { return _bestVolumeMl; }
  uint16_t bestStableMs()  const { return _bestStableMs; }
  uint32_t attemptCount()  const { return _attemptCount; }
  uint32_t successCount()  const { return _successCount; }

  void printStats() const;

private:
  Preferences _prefs;
  float    _bestVolumeMl = 0.0f;
  uint16_t _bestStableMs = 0;
  uint32_t _attemptCount = 0;
  uint32_t _successCount = 0;
};
