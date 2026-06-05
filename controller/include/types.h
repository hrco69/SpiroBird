// ============================================================================
// SpiroBird Controller — internal shared types
// ============================================================================
#pragma once
#include <Arduino.h>
#include "protocol.h"   // ExerciseState, FailReason

// Result of one finished attempt (success or fail).
// Filled by ExerciseLogic, consumed by Storage (high score) and
// ServerClient (HTTP POST, Faza 4).
struct AttemptResult {
  bool       valid        = false;
  bool       success      = false;
  FailReason failReason   = FAIL_NONE;
  float      volumeMl     = 0.0f;
  float      maxFlowMlS   = 0.0f;
  float      avgFlowMlS   = 0.0f;
  uint16_t   stableTimeMs = 0;
  uint32_t   timestampMs  = 0;   // millis() when the attempt ended
};

// One-shot events emitted by ExerciseLogic; main.cpp polls these and triggers
// haptics / storage / network side effects. Logic itself stays side-effect free.
enum class LogicEvent : uint8_t {
  None = 0,
  CalibrationStarted,   // "do not touch the potentiometer"
  ReadyToStart,         // calibration done -> short start beep
  AttemptStarted,       // flow rose above start threshold
  EnteredZone,          // filtered flow entered 900-1200 -> positive beep
  LeftZone,             // left the target zone -> small haptic nudge
  DangerWarning,        // approaching 1200 -> warning pulse
  Success,              // melody + save high score + POST result
  Fail,                 // error beep + fail pulse + POST result
  ResultDone            // result screen finished, back to IDLE
};
