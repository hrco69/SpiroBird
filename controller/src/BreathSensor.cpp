#include "BreathSensor.h"

void BreathSensor::begin() {
  analogReadResolution(12);                          // 0-4095
  analogSetPinAttenuation(PIN_POT_ADC, ADC_11db);    // full 0-3.3 V range
  pinMode(PIN_POT_ADC, INPUT);
  DBG("[sensor] begin: pin=%d, usable ADC range %d-%d, deadzone=%d\n",
      PIN_POT_ADC, ADC_MIN_USABLE, ADC_MAX_USABLE, ADC_DEADZONE);
}

void BreathSensor::startCalibration(uint32_t nowMs) {
  _calibrating = true;
  _calibrated  = false;
  _centerHoldStartMs = 0;
  _lastHintMs  = nowMs;
  DBG("[sensor] calibration: set the knob into the center zone %d-%d and hold it for %d ms\n",
      POT_CENTER_ADC - POT_CENTER_TOLERANCE, POT_CENTER_ADC + POT_CENTER_TOLERANCE,
      POT_CENTER_HOLD_MS);
}

void BreathSensor::update(uint32_t nowMs) {
  _rawAdc = readAveragedAdc();

  if (_calibrating) {
    // Fixed-center calibration: wait until the knob sits inside the center
    // zone continuously for POT_CENTER_HOLD_MS, then fix offset at the
    // designed midpoint (no averaging — the midpoint is always the same).
    const bool inCenter =
        _rawAdc >= POT_CENTER_ADC - POT_CENTER_TOLERANCE &&
        _rawAdc <= POT_CENTER_ADC + POT_CENTER_TOLERANCE;

    if (inCenter) {
      if (_centerHoldStartMs == 0) {
        _centerHoldStartMs = nowMs;
        DBG("[sensor] knob centered (raw=%u) — hold still for %d ms...\n",
            _rawAdc, POT_CENTER_HOLD_MS);
      }
      if (nowMs - _centerHoldStartMs >= POT_CENTER_HOLD_MS) {
        _offsetAdc = POT_CENTER_ADC;   // fixed by design
        uint16_t up   = ADC_MAX_USABLE - _offsetAdc;
        uint16_t down = _offsetAdc - ADC_MIN_USABLE;
        _usableDeviation = (float)max(up, down);

        _calibrating = false;
        _calibrated  = true;
        _filteredFlowMlS = 0.0f;
        resetHistory();
        DBG("[sensor] calibration done: offset fixed at %u, usableDev=%.0f\n",
            _offsetAdc, _usableDeviation);
      }
    } else {
      if (_centerHoldStartMs != 0) {
        DBG("[sensor] knob left the center zone (raw=%u) — re-center and hold again\n",
            _rawAdc);
        _centerHoldStartMs = 0;
      } else if (nowMs - _lastHintMs >= 2000) {
        _lastHintMs = nowMs;
        DBG("[sensor] waiting: turn the knob into %d-%d (raw=%u)\n",
            POT_CENTER_ADC - POT_CENTER_TOLERANCE,
            POT_CENTER_ADC + POT_CENTER_TOLERANCE, _rawAdc);
      }
    }
    return;
  }

  if (!_calibrated) return;

  _deviationAdc = (int16_t)_rawAdc - (int16_t)_offsetAdc;
  float dev = fabsf((float)_deviationAdc);

  // Deadzone around the calibrated center -> flow 0, then linear map to
  // 0..FLOW_MAX_ML_S over the remaining usable deviation.
  float flow = 0.0f;
  if (dev > ADC_DEADZONE) {
    flow = (dev - ADC_DEADZONE) / (_usableDeviation - ADC_DEADZONE) * FLOW_MAX_ML_S;
    flow = constrain(flow, 0.0f, FLOW_MAX_ML_S);
  }
  _flowMlS = flow;

  // EMA low-pass: light enough to avoid lag, heavy enough to kill ADC noise.
  _filteredFlowMlS = EMA_ALPHA * flow + (1.0f - EMA_ALPHA) * _filteredFlowMlS;

  pushHistory(_filteredFlowMlS);
}

uint16_t BreathSensor::readAveragedAdc() {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES_PER_READ; i++) {
    sum += analogRead(PIN_POT_ADC);
  }
  uint16_t avg = (uint16_t)(sum / ADC_SAMPLES_PER_READ);
  // Clamp away the nonlinear rail regions.
  if (avg < ADC_MIN_USABLE) avg = ADC_MIN_USABLE;
  if (avg > ADC_MAX_USABLE) avg = ADC_MAX_USABLE;
  return avg;
}

void BreathSensor::pushHistory(float v) {
  _history[_histHead] = v;
  _histHead = (_histHead + 1) % HISTORY_LEN;
  if (_histCount < HISTORY_LEN) _histCount++;
}

void BreathSensor::resetHistory() {
  _histCount = 0;
  _histHead  = 0;
}

float BreathSensor::peakToPeak() const {
  if (_histCount == 0) return 0.0f;
  float mn = _history[0], mx = _history[0];
  for (size_t i = 1; i < _histCount; i++) {
    if (_history[i] < mn) mn = _history[i];
    if (_history[i] > mx) mx = _history[i];
  }
  return mx - mn;
}

bool BreathSensor::inTargetZone() const {
  return _filteredFlowMlS >= FLOW_TARGET_MIN_ML_S &&
         _filteredFlowMlS <= FLOW_TARGET_MAX_ML_S;
}

bool BreathSensor::inDangerZone() const {
  return _filteredFlowMlS > FLOW_TARGET_MAX_ML_S;
}

bool BreathSensor::isStableNow() const {
  // Stability never relies on a single sample: the whole window must be
  // filled and its peak-to-peak variation below the configured limit.
  return _histCount >= HISTORY_LEN &&
         inTargetZone() &&
         peakToPeak() < STABILITY_P2P_MAX_ML_S;
}

bool BreathSensor::flowDetected() const {
  return _filteredFlowMlS > FLOW_START_THRESHOLD_ML_S;
}
