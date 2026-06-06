// ============================================================================
// ExerciseLogic — the SpiroBird exercise state machine (single source of truth)
//
//   IDLE -> CALIBRATING -> READY -> ACTIVE -> SUCCESS/FAIL -> RESULT -> IDLE
//                                          \-> STATE_SLEEP (inactivity)
//
// Success: filtered flow stable inside 900-1200 ml/s continuously for 5000 ms.
// Fail:    filtered > 1200 ml/s OR raw > 1250 ml/s -> FAIL_OVER_1200 (instant),
//          or attempt timeout -> FAIL_TIMEOUT.
//
// The logic is side-effect free: it emits LogicEvents which main.cpp turns
// into haptics / storage / network actions. Every state transition is printed
// to Serial:  "STATE_ACTIVE -> STATE_SUCCESS".
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "BreathSensor.h"

class ExerciseLogic {
public:
  void begin(BreathSensor *sensor);

  // Call once per sample tick, AFTER sensor->update().
  void update(uint32_t nowMs);

  // External inputs
  void onButtonPressed(uint32_t nowMs);
  void requestStart(uint32_t nowMs);       // start (calibration) from IDLE
  void enterSleep(uint32_t nowMs);         // pseudo sleep (main decides when)
  void wakeUp(uint32_t nowMs);             // leave STATE_SLEEP -> IDLE

  // State / telemetry (consumed by packet builder + debug prints)
  ExerciseState state()      const { return _state; }
  FailReason failReason()    const { return _failReason; }
  uint16_t stableTimeMs()    const { return _stableTimeMs; }
  float volumeMl()           const { return _volumeMl; }
  float maxFlowMlS()         const { return _maxFlowMlS; }
  float avgFlowMlS()         const { return _flowCount ? _flowSum / _flowCount : 0.0f; }
  const AttemptResult &result() const { return _result; }
  static const char *stateName(ExerciseState s);

  // Event queue — main.cpp drains this every loop.
  LogicEvent nextEvent();

private:
  void setState(ExerciseState s, uint32_t nowMs);
  void startAttempt(uint32_t nowMs);
  void updateActive(uint32_t nowMs);
  void finishAttempt(bool success, FailReason reason, uint32_t nowMs);
  void resetAttempt();
  void pushEvent(LogicEvent e);

  BreathSensor *_sensor = nullptr;

  ExerciseState _state      = STATE_IDLE;
  FailReason    _failReason = FAIL_NONE;
  uint32_t _stateEnteredMs  = 0;
  bool     _startRequested  = false;
  bool     _idleArmed       = false;   // knob returned to rest while in IDLE

  // Attempt tracking
  uint32_t _attemptStartMs = 0;
  uint32_t _stableStartMs  = 0;     // 0 = currently not stable
  uint16_t _stableTimeMs   = 0;
  float    _volumeMl       = 0.0f;
  float    _maxFlowMlS     = 0.0f;
  float    _flowSum        = 0.0f;
  uint32_t _flowCount      = 0;
  bool     _wasInZone      = false;
  bool     _warnLatched    = false;
  AttemptResult _result;

  // Small ring queue of one-shot events (8 is plenty at 100 Hz polling)
  static constexpr size_t EVENT_QUEUE_LEN = 8;
  LogicEvent _events[EVENT_QUEUE_LEN];
  size_t _evHead = 0, _evTail = 0;
};
