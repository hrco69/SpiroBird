// ============================================================================
// BreathSensor — potentiometer as breath-flow emulator
//
// Responsibilities:
//  - averaged 12-bit ADC reads, clamped to the usable range (200-3900)
//  - fixed-center calibration: waits (non-blocking) until the knob sits in
//    the POT_CENTER_ADC +/- POT_CENTER_TOLERANCE zone for POT_CENTER_HOLD_MS,
//    then the zero-flow offset is FIXED at POT_CENTER_ADC
//  - deadzone + deviation -> flow mapping (0..FLOW_MAX_ML_S)
//  - EMA low-pass filter
//  - 500 ms ring buffer of filtered flow for peak-to-peak stability check
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

class BreathSensor {
public:
  void begin();

  // Non-blocking calibration: call startCalibration(), then keep calling
  // update() at the sample rate; isCalibrating() turns false once the knob
  // has stayed in the center zone for POT_CENTER_HOLD_MS.
  void startCalibration(uint32_t nowMs);
  bool isCalibrating() const { return _calibrating; }
  bool isCalibrated()  const { return _calibrated; }

  // Call exactly once per SENSOR_SAMPLE_INTERVAL_MS tick.
  void update(uint32_t nowMs);

  uint16_t rawAdc()          const { return _rawAdc; }
  uint16_t offsetAdc()       const { return _offsetAdc; }
  int16_t  deviationAdc()    const { return _deviationAdc; }
  float    flowMlS()         const { return _flowMlS; }          // unfiltered
  float    filteredFlowMlS() const { return _filteredFlowMlS; }  // EMA

  float peakToPeak() const;   // over the stability window
  bool  inTargetZone() const; // 900 <= filtered <= 1200
  bool  inDangerZone() const; // filtered > 1200
  bool  isStableNow() const;  // in zone + p2p below limit + window filled
  bool  flowDetected() const; // filtered above start threshold

private:
  uint16_t readAveragedAdc();
  void     pushHistory(float v);
  void     resetHistory();

  static constexpr size_t HISTORY_LEN =
      STABILITY_WINDOW_MS / SENSOR_SAMPLE_INTERVAL_MS;   // 50 samples @ 100 Hz

  float  _history[HISTORY_LEN] = {0};
  size_t _histCount = 0;
  size_t _histHead  = 0;

  uint16_t _rawAdc          = 0;
  uint16_t _offsetAdc       = 0;
  int16_t  _deviationAdc    = 0;
  float    _flowMlS         = 0.0f;
  float    _filteredFlowMlS = 0.0f;
  float    _usableDeviation = 1.0f;   // max deviation from offset within usable range

  bool     _calibrated  = false;
  bool     _calibrating = false;
  uint32_t _centerHoldStartMs = 0;   // 0 = knob currently outside center zone
  uint32_t _lastHintMs        = 0;   // rate limit for "move to center" hints
};
