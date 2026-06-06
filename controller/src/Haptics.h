// ============================================================================
// Haptics — passive buzzer (LEDC tones) + vibration motor (NPN low-side)
//
// 100 % non-blocking: melodies are step sequences advanced from update(),
// motor pulses are timed in update(). NO delay() anywhere.
//
// Motor safety (3 V motor on a 5 V supply!):
//  - pulse length hard-clamped to MOTOR_MAX_PULSE_MS
//  - enforced cooldown MOTOR_COOLDOWN_MS between pulses
//  - watchdog in update(): motor can never stay on past the hard limit
//  - everything compiled out with ENABLE_MOTOR 0
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

class Haptics {
public:
  void begin();
  void update(uint32_t nowMs);   // call every loop iteration

  // Buzzer (no-ops when ENABLE_BUZZER 0)
  void beep(uint16_t freqHz, uint16_t durationMs);
  void bootChirp();      // boot-time hardware self-test (3 rising chirps)
  void startBeep();      // short beep when READY
  void zoneBeep();       // positive blip on entering the target zone
  void successMelody();  // longer melody on success
  void errorBeep();      // harsh beep on fail / over 1200

  // Motor (no-ops when ENABLE_MOTOR 0). Patterns are sequences of ON/OFF
  // segments played non-blocking from update(); each ON segment is hard-
  // capped at MOTOR_MAX_PULSE_MS and a cooldown separates whole patterns.
  struct MotorStep { uint16_t onMs; uint16_t offMs; };

  void motorPulse(uint16_t durationMs);                 // single pulse
  void motorPattern(const MotorStep *steps, size_t count);
  void motorWarn()      { motorPulse(MOTOR_PULSE_WARN_MS); }
  void motorCollision() { motorPulse(MOTOR_PULSE_COLLISION_MS); }
  void motorFail();     // MOTOR_FAIL_PULSE_COUNT long pulses in a row

  void allOff();   // immediate silence + motor off (sleep / shutdown)

  static constexpr size_t MOTOR_MAX_STEPS = 4;

private:
  struct ToneStep { uint16_t freqHz; uint16_t durationMs; };  // freq 0 = rest

  void playSequence(const ToneStep *steps, size_t count);
  void applyStep(uint32_t nowMs);
  void toneOn(uint16_t freqHz);
  void toneOff();
  void motorFinish(uint32_t nowMs);

  static constexpr size_t MAX_STEPS = 8;
  ToneStep _seq[MAX_STEPS];
  size_t   _seqLen = 0, _seqIdx = 0;
  uint32_t _stepStartMs = 0;
  bool     _playing = false;

  MotorStep _mSeq[MOTOR_MAX_STEPS];
  size_t   _mLen = 0, _mIdx = 0;
  bool     _motorActive = false;
  bool     _mPhaseOn = false;
  uint32_t _mPhaseStartMs = 0;
  uint32_t _motorLastOffMs = 0;
};
