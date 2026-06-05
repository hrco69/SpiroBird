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
  void startBeep();      // short beep when READY
  void zoneBeep();       // positive blip on entering the target zone
  void successMelody();  // longer melody on success
  void errorBeep();      // harsh beep on fail / over 1200

  // Motor (no-ops when ENABLE_MOTOR 0)
  void motorPulse(uint16_t durationMs);  // clamped + cooldown enforced
  void motorWarn()      { motorPulse(MOTOR_PULSE_WARN_MS); }
  void motorCollision() { motorPulse(MOTOR_PULSE_COLLISION_MS); }
  void motorFail()      { motorPulse(MOTOR_PULSE_FAIL_MS); }

  void allOff();   // immediate silence + motor off (sleep / shutdown)

private:
  struct ToneStep { uint16_t freqHz; uint16_t durationMs; };  // freq 0 = rest

  void playSequence(const ToneStep *steps, size_t count);
  void applyStep(uint32_t nowMs);
  void toneOn(uint16_t freqHz);
  void toneOff();

  static constexpr size_t MAX_STEPS = 8;
  ToneStep _seq[MAX_STEPS];
  size_t   _seqLen = 0, _seqIdx = 0;
  uint32_t _stepStartMs = 0;
  bool     _playing = false;

  bool     _motorOn = false;
  uint32_t _motorOnMs = 0;
  uint16_t _motorDurMs = 0;
  uint32_t _motorLastOffMs = 0;
};
