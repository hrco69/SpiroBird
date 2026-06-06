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
  _calEndMs    = nowMs + CALIBRATION_DURATION_MS;
  _calSum      = 0;
  _calCount    = 0;
  DBG("[sensor] calibration started (%d ms) — CENTER the potentiometer and do not touch it\n",
      CALIBRATION_DURATION_MS);
}

void BreathSensor::update(uint32_t nowMs) {
  _rawAdc = readAveragedAdc();

  if (_calibrating) {
    _calSum += _rawAdc;
    _calCount++;
    if ((int32_t)(nowMs - _calEndMs) >= 0 && _calCount > 0) {
      _offsetAdc = (uint16_t)(_calSum / _calCount);
      // Largest deviation reachable from the calibrated center while staying
      // inside the usable ADC range — this maps to FLOW_MAX_ML_S.
      uint16_t up   = ADC_MAX_USABLE - _offsetAdc;
      uint16_t down = _offsetAdc - ADC_MIN_USABLE;
      _usableDeviation = (float)max(up, down);
      if (_usableDeviation <= ADC_DEADZONE + 1) _usableDeviation = ADC_DEADZONE + 100;

      _calibrating = false;
      _calibrated  = true;
      _filteredFlowMlS = 0.0f;
      resetHistory();
      DBG("[sensor] calibration done: offset=%u (expected ~2048), usableDev=%.0f, samples=%u\n",
          _offsetAdc, _usableDeviation, _calCount);
      if (_offsetAdc >= ADC_MAX_USABLE - 300 || _offsetAdc <= ADC_MIN_USABLE + 300) {
        DBG("[sensor] *** WARNING: offset is at an ADC rail! ***\n");
        DBG("[sensor] *** Center the potentiometer, then press the button (or RST) to recalibrate. ***\n");
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
