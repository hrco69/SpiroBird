"""
SpiroBird — 1:1 Python port of the Controller firmware logic.

Mirrors controller/src/BreathSensor.cpp and ExerciseLogic.cpp so the exercise
logic can be verified BEFORE flashing hardware (bring-up philosophy).
The only difference: the ADC value is injected instead of read from a pin.

!! Constants MUST stay in sync with controller/include/config.h !!
"""

from dataclasses import dataclass, field

# --- constants from controller/include/config.h ------------------------------
ADC_MIN_USABLE = 200
ADC_MAX_USABLE = 3900
ADC_DEADZONE = 70
SENSOR_SAMPLE_INTERVAL_MS = 10            # 100 Hz

# Fixed-center calibration (matches config.h after hardware bring-up):
POT_CENTER_ADC = 2000
POT_CENTER_TOLERANCE = 300                # accepted zone 1700-2300
POT_CENTER_HOLD_MS = 2000                 # must stay centered this long

FLOW_MAX_ML_S = 1400.0
EMA_ALPHA = 0.20

FLOW_LINE_LOW_ML_S = 600.0
FLOW_TARGET_MIN_ML_S = 900.0
FLOW_TARGET_MAX_ML_S = 1200.0
FLOW_FAIL_RAW_ML_S = 1250.0
FLOW_WARN_ML_S = 1150.0
FLOW_START_THRESHOLD_ML_S = 150.0

STABILITY_WINDOW_MS = 500
STABILITY_P2P_MAX_ML_S = 180.0
STABLE_SUCCESS_MS = 5000

ATTEMPT_TIMEOUT_MS = 60000
SUCCESS_TO_RESULT_MS = 1200
RESULT_DURATION_MS = 5000

HISTORY_LEN = STABILITY_WINDOW_MS // SENSOR_SAMPLE_INTERVAL_MS   # 50 samples

# --- protocol enums (protocol.h) ---------------------------------------------
STATE_IDLE, STATE_CALIBRATING, STATE_READY, STATE_ACTIVE = 0, 1, 2, 3
STATE_SUCCESS, STATE_FAIL, STATE_RESULT, STATE_SLEEP = 4, 5, 6, 7
STATE_NAMES = {
    0: "STATE_IDLE", 1: "STATE_CALIBRATING", 2: "STATE_READY",
    3: "STATE_ACTIVE", 4: "STATE_SUCCESS", 5: "STATE_FAIL",
    6: "STATE_RESULT", 7: "STATE_SLEEP",
}
FAIL_NONE, FAIL_OVER_1200, FAIL_UNSTABLE, FAIL_COLLISION, FAIL_TIMEOUT = 0, 1, 2, 3, 4


def flow_to_adc(flow_ml_s: float, offset: int = POT_CENTER_ADC) -> int:
    """Inverse of the firmware flow mapping — converts a desired flow into the
    raw ADC value a potentiometer would have to produce (positive deviation)."""
    usable_dev = float(max(ADC_MAX_USABLE - offset, offset - ADC_MIN_USABLE))
    if flow_ml_s <= 0:
        return offset
    dev = ADC_DEADZONE + (flow_ml_s / FLOW_MAX_ML_S) * (usable_dev - ADC_DEADZONE)
    return min(int(round(offset + dev)), ADC_MAX_USABLE)


class BreathSensor:
    """Port of BreathSensor.cpp (ADC value injected via update())."""

    def __init__(self):
        self.raw_adc = 0
        self.offset_adc = 0
        self.deviation_adc = 0
        self.flow_ml_s = 0.0
        self.filtered_flow_ml_s = 0.0
        self.usable_deviation = 1.0
        self.calibrated = False
        self.calibrating = False
        self._center_hold_start_ms = 0
        self._history: list[float] = []

    def start_calibration(self, now_ms: int):
        self.calibrating = True
        self.calibrated = False
        self._center_hold_start_ms = 0

    def update(self, now_ms: int, adc_value: int):
        # clamp like readAveragedAdc()
        self.raw_adc = max(ADC_MIN_USABLE, min(ADC_MAX_USABLE, int(adc_value)))

        if self.calibrating:
            # Fixed-center calibration: knob must sit in the center zone
            # continuously for POT_CENTER_HOLD_MS; offset is then FIXED.
            in_center = (POT_CENTER_ADC - POT_CENTER_TOLERANCE
                         <= self.raw_adc
                         <= POT_CENTER_ADC + POT_CENTER_TOLERANCE)
            if in_center:
                if self._center_hold_start_ms == 0:
                    self._center_hold_start_ms = now_ms
                if now_ms - self._center_hold_start_ms >= POT_CENTER_HOLD_MS:
                    self.offset_adc = POT_CENTER_ADC
                    up = ADC_MAX_USABLE - self.offset_adc
                    down = self.offset_adc - ADC_MIN_USABLE
                    self.usable_deviation = float(max(up, down))
                    self.calibrating = False
                    self.calibrated = True
                    self.filtered_flow_ml_s = 0.0
                    self._history.clear()
            else:
                self._center_hold_start_ms = 0
            return

        if not self.calibrated:
            return

        self.deviation_adc = self.raw_adc - self.offset_adc
        dev = abs(float(self.deviation_adc))

        flow = 0.0
        if dev > ADC_DEADZONE:
            flow = (dev - ADC_DEADZONE) / (self.usable_deviation - ADC_DEADZONE) * FLOW_MAX_ML_S
            flow = max(0.0, min(FLOW_MAX_ML_S, flow))
        self.flow_ml_s = flow

        self.filtered_flow_ml_s = EMA_ALPHA * flow + (1.0 - EMA_ALPHA) * self.filtered_flow_ml_s

        self._history.append(self.filtered_flow_ml_s)
        if len(self._history) > HISTORY_LEN:
            self._history.pop(0)

    # --- accessors mirroring the C++ API ---
    def peak_to_peak(self) -> float:
        if not self._history:
            return 0.0
        return max(self._history) - min(self._history)

    def in_target_zone(self) -> bool:
        return FLOW_TARGET_MIN_ML_S <= self.filtered_flow_ml_s <= FLOW_TARGET_MAX_ML_S

    def in_danger_zone(self) -> bool:
        return self.filtered_flow_ml_s > FLOW_TARGET_MAX_ML_S

    def is_stable_now(self) -> bool:
        return (len(self._history) >= HISTORY_LEN
                and self.in_target_zone()
                and self.peak_to_peak() < STABILITY_P2P_MAX_ML_S)

    def flow_detected(self) -> bool:
        return self.filtered_flow_ml_s > FLOW_START_THRESHOLD_ML_S


@dataclass
class AttemptResult:
    valid: bool = False
    success: bool = False
    fail_reason: int = FAIL_NONE
    volume_ml: float = 0.0
    max_flow_ml_s: float = 0.0
    avg_flow_ml_s: float = 0.0
    stable_time_ms: int = 0
    timestamp_ms: int = 0


@dataclass
class ExerciseLogic:
    """Port of ExerciseLogic.cpp — same states, same ordering of checks."""

    sensor: BreathSensor
    attempt_timeout_ms: int = ATTEMPT_TIMEOUT_MS   # overridable for fast tests

    state: int = STATE_IDLE
    fail_reason: int = FAIL_NONE
    stable_time_ms: int = 0
    volume_ml: float = 0.0
    max_flow_ml_s: float = 0.0
    result: AttemptResult = field(default_factory=AttemptResult)
    transitions: list = field(default_factory=list)   # (ms, from, to)
    events: list = field(default_factory=list)        # (ms, name)

    _state_entered_ms: int = 0
    _start_requested: bool = False
    _idle_armed: bool = False
    _attempt_start_ms: int = 0
    _stable_start_ms: int = 0
    _flow_sum: float = 0.0
    _flow_count: int = 0
    _was_in_zone: bool = False

    def avg_flow(self) -> float:
        return self._flow_sum / self._flow_count if self._flow_count else 0.0

    def _set_state(self, s: int, now_ms: int):
        if s == self.state:
            return
        self.transitions.append((now_ms, STATE_NAMES[self.state], STATE_NAMES[s]))
        self.state = s
        self._state_entered_ms = now_ms

    def _push_event(self, name: str, now_ms: int):
        self.events.append((now_ms, name))

    def request_start(self, now_ms: int):
        if self.state == STATE_IDLE:
            self._start_requested = True

    def _reset_attempt(self):
        self._attempt_start_ms = 0
        self._stable_start_ms = 0
        self.stable_time_ms = 0
        self.volume_ml = 0.0
        self.max_flow_ml_s = 0.0
        self._flow_sum = 0.0
        self._flow_count = 0
        self._was_in_zone = False
        self.fail_reason = FAIL_NONE

    def update(self, now_ms: int):
        s = self.state
        if s == STATE_IDLE:
            if self._start_requested:
                # button -> full recalibration
                self._start_requested = False
                self._reset_attempt()
                self.sensor.start_calibration(now_ms)
                self._set_state(STATE_CALIBRATING, now_ms)
                self._push_event("CalibrationStarted", now_ms)
            elif self.sensor.calibrated:
                # flow-detected start: re-arms only after returning to rest
                if not self._idle_armed:
                    if self.sensor.filtered_flow_ml_s < FLOW_START_THRESHOLD_ML_S * 0.5:
                        self._idle_armed = True
                elif self.sensor.flow_detected():
                    self._idle_armed = False
                    self._reset_attempt()
                    self._set_state(STATE_READY, now_ms)
                    self._push_event("ReadyToStart", now_ms)
        elif s == STATE_CALIBRATING:
            if not self.sensor.calibrating:
                self._set_state(STATE_READY, now_ms)
                self._push_event("ReadyToStart", now_ms)
        elif s == STATE_READY:
            if self.sensor.flow_detected():
                self._reset_attempt()
                self._attempt_start_ms = now_ms
                self._set_state(STATE_ACTIVE, now_ms)
                self._push_event("AttemptStarted", now_ms)
        elif s == STATE_ACTIVE:
            self._update_active(now_ms)
        elif s in (STATE_SUCCESS, STATE_FAIL):
            if now_ms - self._state_entered_ms >= SUCCESS_TO_RESULT_MS:
                self._set_state(STATE_RESULT, now_ms)
        elif s == STATE_RESULT:
            if now_ms - self._state_entered_ms >= RESULT_DURATION_MS:
                self._set_state(STATE_IDLE, now_ms)
                self._push_event("ResultDone", now_ms)

    def _update_active(self, now_ms: int):
        f = self.sensor.filtered_flow_ml_s
        dt = SENSOR_SAMPLE_INTERVAL_MS / 1000.0

        self.volume_ml += f * dt
        self._flow_sum += f
        self._flow_count += 1
        if f > self.max_flow_ml_s:
            self.max_flow_ml_s = f

        in_zone = self.sensor.in_target_zone()
        if in_zone != self._was_in_zone:
            self._push_event("EnteredZone" if in_zone else "LeftZone", now_ms)
            self._was_in_zone = in_zone

        # FAIL: over the limit (filtered > 1200 or raw spike > 1250)
        if f > FLOW_TARGET_MAX_ML_S or self.sensor.flow_ml_s > FLOW_FAIL_RAW_ML_S:
            self._finish(False, FAIL_OVER_1200, now_ms)
            return

        # SUCCESS: continuously stable for STABLE_SUCCESS_MS
        if self.sensor.is_stable_now():
            if self._stable_start_ms == 0:
                self._stable_start_ms = now_ms
            stable = now_ms - self._stable_start_ms
            self.stable_time_ms = min(stable, STABLE_SUCCESS_MS)
            if stable >= STABLE_SUCCESS_MS:
                self._finish(True, FAIL_NONE, now_ms)
                return
        else:
            self._stable_start_ms = 0
            self.stable_time_ms = 0

        if now_ms - self._attempt_start_ms >= self.attempt_timeout_ms:
            self._finish(False, FAIL_TIMEOUT, now_ms)

    def _finish(self, success: bool, reason: int, now_ms: int):
        self.fail_reason = reason
        self.result = AttemptResult(
            valid=True, success=success, fail_reason=reason,
            volume_ml=self.volume_ml, max_flow_ml_s=self.max_flow_ml_s,
            avg_flow_ml_s=self.avg_flow(), stable_time_ms=self.stable_time_ms,
            timestamp_ms=now_ms,
        )
        if success:
            self._set_state(STATE_SUCCESS, now_ms)
            self._push_event("Success", now_ms)
        else:
            self._set_state(STATE_FAIL, now_ms)
            self._push_event("Fail", now_ms)


# --- protocol checksum port (protocol.h) --------------------------------------
import struct

# Little-endian packed layout of SpiroPacket — see docs/protocol.md
SPIRO_PACKET_FORMAT = "<HBIIHhfffffHBB?????BBBB"
SPIRO_PACKET_SIZE = 48
SPIROBIRD_MAGIC = 0x5342
SPIROBIRD_PROTOCOL_VERSION = 1


def packet_checksum(packet_bytes: bytes) -> int:
    """XOR of every byte except the final checksum byte."""
    x = 0
    for b in packet_bytes[:-1]:
        x ^= b
    return x
