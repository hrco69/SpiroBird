#include "ExerciseLogic.h"

void ExerciseLogic::begin(BreathSensor *sensor) {
  _sensor = sensor;
  _state = STATE_IDLE;
  _stateEnteredMs = millis();
  resetAttempt();
}

const char *ExerciseLogic::stateName(ExerciseState s) {
  switch (s) {
    case STATE_IDLE:        return "STATE_IDLE";
    case STATE_CALIBRATING: return "STATE_CALIBRATING";
    case STATE_READY:       return "STATE_READY";
    case STATE_ACTIVE:      return "STATE_ACTIVE";
    case STATE_SUCCESS:     return "STATE_SUCCESS";
    case STATE_FAIL:        return "STATE_FAIL";
    case STATE_RESULT:      return "STATE_RESULT";
    case STATE_SLEEP:       return "STATE_SLEEP";
  }
  return "STATE_?";
}

void ExerciseLogic::setState(ExerciseState s, uint32_t nowMs) {
  if (s == _state) return;
  DBG("[logic] %s -> %s\n", stateName(_state), stateName(s));
  _state = s;
  _stateEnteredMs = nowMs;
}

void ExerciseLogic::pushEvent(LogicEvent e) {
  size_t next = (_evHead + 1) % EVENT_QUEUE_LEN;
  if (next == _evTail) return;   // queue full -> drop (never blocks)
  _events[_evHead] = e;
  _evHead = next;
}

LogicEvent ExerciseLogic::nextEvent() {
  if (_evTail == _evHead) return LogicEvent::None;
  LogicEvent e = _events[_evTail];
  _evTail = (_evTail + 1) % EVENT_QUEUE_LEN;
  return e;
}

void ExerciseLogic::onButtonPressed(uint32_t nowMs) {
  switch (_state) {
    case STATE_IDLE:
      requestStart(nowMs);
      break;
    case STATE_RESULT:
      // Skip the result screen and start a fresh round immediately.
      setState(STATE_IDLE, nowMs);
      pushEvent(LogicEvent::ResultDone);
      requestStart(nowMs);
      break;
    default:
      break;   // sleep wake is handled by main.cpp (it owns sleep policy)
  }
}

void ExerciseLogic::requestStart(uint32_t nowMs) {
  if (_state == STATE_IDLE) _startRequested = true;
  (void)nowMs;
}

void ExerciseLogic::enterSleep(uint32_t nowMs) {
  resetAttempt();
  setState(STATE_SLEEP, nowMs);
}

void ExerciseLogic::wakeUp(uint32_t nowMs) {
  if (_state == STATE_SLEEP) setState(STATE_IDLE, nowMs);
}

void ExerciseLogic::resetAttempt() {
  _attemptStartMs = 0;
  _stableStartMs  = 0;
  _stableTimeMs   = 0;
  _volumeMl       = 0.0f;
  _maxFlowMlS     = 0.0f;
  _flowSum        = 0.0f;
  _flowCount      = 0;
  _wasInZone      = false;
  _warnLatched    = false;
  _failReason     = FAIL_NONE;
}

void ExerciseLogic::update(uint32_t nowMs) {
  switch (_state) {

    case STATE_IDLE:
      if (_startRequested) {
        _startRequested = false;
        resetAttempt();
        _sensor->startCalibration(nowMs);
        setState(STATE_CALIBRATING, nowMs);
        pushEvent(LogicEvent::CalibrationStarted);
      }
      break;

    case STATE_CALIBRATING:
      if (!_sensor->isCalibrating()) {
        setState(STATE_READY, nowMs);
        pushEvent(LogicEvent::ReadyToStart);
      }
      break;

    case STATE_READY:
      // User starts "breathing": flow rises above the start threshold.
      if (_sensor->flowDetected()) startAttempt(nowMs);
      break;

    case STATE_ACTIVE:
      updateActive(nowMs);
      break;

    case STATE_SUCCESS:
    case STATE_FAIL:
      // Short distinct success/fail flash, then the result summary.
      if (nowMs - _stateEnteredMs >= SUCCESS_TO_RESULT_MS) {
        setState(STATE_RESULT, nowMs);
      }
      break;

    case STATE_RESULT:
      if (nowMs - _stateEnteredMs >= RESULT_DURATION_MS) {
        setState(STATE_IDLE, nowMs);
        pushEvent(LogicEvent::ResultDone);
      }
      break;

    case STATE_SLEEP:
      break;   // main.cpp wakes us
  }
}

void ExerciseLogic::startAttempt(uint32_t nowMs) {
  resetAttempt();
  _attemptStartMs = nowMs;
  setState(STATE_ACTIVE, nowMs);
  pushEvent(LogicEvent::AttemptStarted);
}

void ExerciseLogic::updateActive(uint32_t nowMs) {
  const float f   = _sensor->filteredFlowMlS();
  const float dt  = SENSOR_SAMPLE_INTERVAL_MS / 1000.0f;

  // Volume by numerical integration of the filtered flow.
  _volumeMl += f * dt;
  _flowSum  += f;
  _flowCount++;
  if (f > _maxFlowMlS) _maxFlowMlS = f;

  // Zone enter/leave events (haptic/buzzer coaching).
  const bool inZone = _sensor->inTargetZone();
  if (inZone != _wasInZone) {
    pushEvent(inZone ? LogicEvent::EnteredZone : LogicEvent::LeftZone);
    _wasInZone = inZone;
  }

  // Early warning when approaching the 1200 ml/s danger limit (latched so it
  // fires once per excursion, re-arms 50 ml/s below the warning level).
  if (f >= FLOW_WARN_ML_S && !_warnLatched) {
    pushEvent(LogicEvent::DangerWarning);
    _warnLatched = true;
  } else if (f < FLOW_WARN_ML_S - 50.0f) {
    _warnLatched = false;
  }

  // FAIL: over the limit — filtered over 1200 or raw spike over 1250.
  if (f > FLOW_TARGET_MAX_ML_S || _sensor->flowMlS() > FLOW_FAIL_RAW_ML_S) {
    finishAttempt(false, FAIL_OVER_1200, nowMs);
    return;
  }

  // SUCCESS: continuously stable for STABLE_SUCCESS_MS.
  if (_sensor->isStableNow()) {
    if (_stableStartMs == 0) _stableStartMs = nowMs;
    uint32_t stable = nowMs - _stableStartMs;
    _stableTimeMs = (uint16_t)min(stable, (uint32_t)STABLE_SUCCESS_MS);
    if (stable >= STABLE_SUCCESS_MS) {
      finishAttempt(true, FAIL_NONE, nowMs);
      return;
    }
  } else {
    _stableStartMs = 0;
    _stableTimeMs  = 0;
  }

  // FAIL: attempt ran way too long without success.
  if (nowMs - _attemptStartMs >= ATTEMPT_TIMEOUT_MS) {
    finishAttempt(false, FAIL_TIMEOUT, nowMs);
  }
}

void ExerciseLogic::finishAttempt(bool success, FailReason reason, uint32_t nowMs) {
  _failReason = reason;

  _result.valid        = true;
  _result.success      = success;
  _result.failReason   = reason;
  _result.volumeMl     = _volumeMl;
  _result.maxFlowMlS   = _maxFlowMlS;
  _result.avgFlowMlS   = avgFlowMlS();
  _result.stableTimeMs = _stableTimeMs;
  _result.timestampMs  = nowMs;

  DBG("[logic] attempt finished: %s vol=%.0f ml, max=%.0f, avg=%.0f, stable=%u ms%s%s\n",
      success ? "SUCCESS" : "FAIL",
      _result.volumeMl, _result.maxFlowMlS, _result.avgFlowMlS, _result.stableTimeMs,
      success ? "" : ", reason=", success ? "" :
        (reason == FAIL_OVER_1200 ? "FAIL_OVER_1200" :
         reason == FAIL_TIMEOUT   ? "FAIL_TIMEOUT" : "FAIL_OTHER"));

  if (success) {
    setState(STATE_SUCCESS, nowMs);
    pushEvent(LogicEvent::Success);
  } else {
    setState(STATE_FAIL, nowMs);
    pushEvent(LogicEvent::Fail);
  }
}
