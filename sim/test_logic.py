"""
SpiroBird — unit tests for the exercise logic (run BEFORE flashing hardware).

Usage:
    cd sim
    python test_logic.py        # or: python -m unittest test_logic -v

Covers: EMA filter, flow mapping, stability detection, volume integration,
success after 5 s, fail above 1200 ml/s (filtered + raw spike), timeout fail,
state transitions, and the SpiroPacket binary layout/checksum.
"""

import struct
import unittest

from spiro_model import (
    ADC_DEADZONE, ADC_MAX_USABLE, POT_CENTER_ADC, POT_CENTER_HOLD_MS,
    EMA_ALPHA,
    FLOW_MAX_ML_S, FLOW_TARGET_MIN_ML_S, FLOW_TARGET_MAX_ML_S,
    HISTORY_LEN, SENSOR_SAMPLE_INTERVAL_MS, STABLE_SUCCESS_MS,
    STATE_ACTIVE, STATE_CALIBRATING, STATE_FAIL, STATE_IDLE, STATE_READY,
    STATE_RESULT, STATE_SUCCESS,
    FAIL_OVER_1200, FAIL_TIMEOUT,
    SPIRO_PACKET_FORMAT, SPIRO_PACKET_SIZE, SPIROBIRD_MAGIC,
    SPIROBIRD_PROTOCOL_VERSION,
    BreathSensor, ExerciseLogic, flow_to_adc, packet_checksum,
)

OFFSET = POT_CENTER_ADC   # zero-flow point is fixed at 2000 by design
TICK = SENSOR_SAMPLE_INTERVAL_MS


def make_calibrated_sensor() -> BreathSensor:
    """Run a real calibration at the rest position (offset = 2048)."""
    s = BreathSensor()
    s.start_calibration(0)
    t = 0
    while s.calibrating:
        t += TICK
        s.update(t, OFFSET)
    return s


def run_exercise(flow_profile, attempt_timeout_ms=60000):
    """Drive sensor+logic with flow_profile(t_ms) -> ml/s. Returns (sensor, logic, t)."""
    sensor = BreathSensor()
    logic = ExerciseLogic(sensor, attempt_timeout_ms=attempt_timeout_ms)
    logic.request_start(0)
    t = 0
    # generous upper bound so every scenario terminates
    while t < attempt_timeout_ms + POT_CENTER_HOLD_MS + 20000:
        t += TICK
        flow = flow_profile(t) if logic.state != STATE_CALIBRATING else 0.0
        sensor.update(t, flow_to_adc(flow, OFFSET))
        logic.update(t)
        if logic.state == STATE_RESULT:
            break
    return sensor, logic, t


class TestEmaFilter(unittest.TestCase):
    def test_first_step_formula(self):
        s = make_calibrated_sensor()
        s.update(10_000, flow_to_adc(1000, OFFSET))
        raw = s.flow_ml_s
        self.assertAlmostEqual(s.filtered_flow_ml_s, EMA_ALPHA * raw, places=3)

    def test_converges_to_constant_input(self):
        s = make_calibrated_sensor()
        for i in range(300):  # 3 s of constant flow
            s.update(10_000 + i * TICK, flow_to_adc(1000, OFFSET))
        self.assertAlmostEqual(s.filtered_flow_ml_s, s.flow_ml_s, delta=1.0)
        self.assertAlmostEqual(s.filtered_flow_ml_s, 1000.0, delta=15.0)

    def test_smooths_spikes(self):
        s = make_calibrated_sensor()
        for i in range(200):
            s.update(10_000 + i * TICK, flow_to_adc(1000, OFFSET))
        s.update(12_010, flow_to_adc(1400, OFFSET))  # single spike
        # one spike sample moves the filtered value by only alpha * delta
        self.assertLess(s.filtered_flow_ml_s, 1000 + EMA_ALPHA * 400 + 20)


class TestFlowMapping(unittest.TestCase):
    def setUp(self):
        self.s = make_calibrated_sensor()
        self.assertEqual(self.s.offset_adc, OFFSET)

    def map_one(self, adc):
        self.s.update(10_000, adc)
        return self.s.flow_ml_s

    def test_rest_is_zero(self):
        self.assertEqual(self.map_one(OFFSET), 0.0)

    def test_deadzone_is_zero(self):
        self.assertEqual(self.map_one(OFFSET + ADC_DEADZONE), 0.0)
        self.assertEqual(self.map_one(OFFSET - ADC_DEADZONE), 0.0)

    def test_max_deviation_maps_to_flow_max(self):
        self.assertAlmostEqual(self.map_one(ADC_MAX_USABLE), FLOW_MAX_ML_S, delta=1.0)

    def test_mapping_is_monotonic(self):
        flows = [self.map_one(OFFSET + d) for d in (100, 400, 800, 1200, 1600)]
        self.assertEqual(flows, sorted(flows))

    def test_negative_deviation_also_maps(self):
        # turning the knob the other way must produce flow too (abs deviation)
        self.assertGreater(self.map_one(OFFSET - 500), 0.0)

    def test_inverse_mapping_roundtrip(self):
        for f in (200, 600, 900, 1000, 1200):
            got = self.map_one(flow_to_adc(f, OFFSET))
            self.assertAlmostEqual(got, f, delta=8.0)  # ADC quantization


class TestStability(unittest.TestCase):
    def test_constant_in_zone_is_stable(self):
        s = make_calibrated_sensor()
        for i in range(HISTORY_LEN * 3):
            s.update(10_000 + i * TICK, flow_to_adc(1000, OFFSET))
        self.assertTrue(s.is_stable_now())

    def test_not_stable_until_window_full(self):
        s = make_calibrated_sensor()
        for i in range(HISTORY_LEN // 2):
            s.update(10_000 + i * TICK, flow_to_adc(1000, OFFSET))
        self.assertFalse(s.is_stable_now())

    def test_high_variance_is_not_stable(self):
        s = make_calibrated_sensor()
        # warm the EMA into the zone first, then oscillate hard
        for i in range(200):
            s.update(10_000 + i * TICK, flow_to_adc(1050, OFFSET))
        for i in range(HISTORY_LEN * 2):
            f = 920 if i % 8 < 4 else 1180   # slow square wave -> big p2p
            s.update(12_010 + i * TICK, flow_to_adc(f, OFFSET))
        self.assertGreater(s.peak_to_peak(), 100.0)

    def test_out_of_zone_is_not_stable(self):
        s = make_calibrated_sensor()
        for i in range(HISTORY_LEN * 3):
            s.update(10_000 + i * TICK, flow_to_adc(700, OFFSET))  # below 900
        self.assertFalse(s.is_stable_now())


class TestVolumeIntegration(unittest.TestCase):
    def test_constant_flow_volume(self):
        # 1000 ml/s held stable -> success at 5 s of STABLE time; volume must be
        # roughly flow * active_time (EMA ramp makes it slightly less).
        _, logic, _ = run_exercise(lambda t: 1000.0)
        self.assertTrue(logic.result.valid and logic.result.success)
        # volume sanity: the >=5 s stable phase alone contributes ~5000 ml at
        # ~1000 ml/s; the EMA ramp-up adds a bit more on top
        self.assertGreater(logic.result.volume_ml, 4800)
        self.assertLess(logic.result.volume_ml, 8000)

    def test_zero_flow_zero_volume(self):
        sensor = make_calibrated_sensor()
        logic = ExerciseLogic(sensor)
        logic.state = STATE_ACTIVE
        for i in range(100):
            t = 10_000 + i * TICK
            sensor.update(t, OFFSET)
            logic.update(t)
        self.assertEqual(logic.volume_ml, 0.0)


class TestSuccessAndFail(unittest.TestCase):
    def test_success_after_5_seconds_stable(self):
        _, logic, _ = run_exercise(lambda t: 1000.0)
        self.assertTrue(logic.result.success)
        self.assertEqual(logic.result.stable_time_ms, STABLE_SUCCESS_MS)
        names = [n for _, n in logic.events]
        for expected in ("CalibrationStarted", "ReadyToStart", "AttemptStarted",
                         "EnteredZone", "Success"):
            self.assertIn(expected, names)
        # stable timer started counting only once the window was inside the zone
        success_t = next(t for t, n in logic.events if n == "Success")
        start_t = next(t for t, n in logic.events if n == "AttemptStarted")
        self.assertGreaterEqual(success_t - start_t, STABLE_SUCCESS_MS)

    def test_fail_when_filtered_exceeds_1200(self):
        # ramp up into the zone then keep pushing over the limit
        _, logic, _ = run_exercise(lambda t: min(1380.0, 0.6 * (t - 2000)) if t > 2000 else 0.0)
        self.assertTrue(logic.result.valid)
        self.assertFalse(logic.result.success)
        self.assertEqual(logic.result.fail_reason, FAIL_OVER_1200)

    def test_fail_on_raw_spike_even_if_filtered_low(self):
        sensor = make_calibrated_sensor()
        logic = ExerciseLogic(sensor)
        logic.state = STATE_ACTIVE
        # filtered is ~0, one raw spike over 1250 must fail immediately
        sensor.update(10_000, flow_to_adc(1300, OFFSET))
        logic.update(10_000)
        self.assertEqual(logic.state, STATE_FAIL)
        self.assertEqual(logic.fail_reason, FAIL_OVER_1200)

    def test_low_flow_never_succeeds_then_times_out(self):
        _, logic, _ = run_exercise(lambda t: 500.0, attempt_timeout_ms=8000)
        self.assertTrue(logic.result.valid)
        self.assertFalse(logic.result.success)
        self.assertEqual(logic.result.fail_reason, FAIL_TIMEOUT)

    def test_unstable_flow_never_succeeds(self):
        # oscillates inside/around the zone with p2p far above the limit
        import math
        _, logic, _ = run_exercise(
            lambda t: 1050.0 + 250.0 * math.sin(t * 0.012),
            attempt_timeout_ms=10000)
        self.assertFalse(logic.result.success)

    def test_state_transition_sequence_success(self):
        _, logic, _ = run_exercise(lambda t: 1000.0)
        seq = [(a, b) for _, a, b in logic.transitions]
        self.assertEqual(seq[0], ("STATE_IDLE", "STATE_CALIBRATING"))
        self.assertEqual(seq[1], ("STATE_CALIBRATING", "STATE_READY"))
        self.assertEqual(seq[2], ("STATE_READY", "STATE_ACTIVE"))
        self.assertEqual(seq[3], ("STATE_ACTIVE", "STATE_SUCCESS"))
        self.assertEqual(seq[4], ("STATE_SUCCESS", "STATE_RESULT"))


class TestProtocolPacket(unittest.TestCase):
    def test_packed_size_is_48_bytes(self):
        self.assertEqual(struct.calcsize(SPIRO_PACKET_FORMAT), SPIRO_PACKET_SIZE)

    def test_checksum_roundtrip(self):
        fields = [
            SPIROBIRD_MAGIC, SPIROBIRD_PROTOCOL_VERSION,  # magic, version
            1234, 56789,                                  # seq, timestampMs
            2500, 452,                                    # rawAdc, deviationAdc
            1010.5, 998.25, 4321.0, 1190.0, 950.0,        # flows/volume
            3500, STATE_ACTIVE, 0,                        # stableTime, state, failReason
            True, False, False, False, False,             # zone/success/fail/sleep flags
            3, 1, 6,                                      # wifi, server, channel
            0,                                            # checksum placeholder
        ]
        raw = bytearray(struct.pack(SPIRO_PACKET_FORMAT, *fields))
        raw[-1] = packet_checksum(bytes(raw))
        # display-side validation logic:
        self.assertEqual(packet_checksum(bytes(raw)), raw[-1])
        # single bit flip must be detected
        raw[20] ^= 0x10
        self.assertNotEqual(packet_checksum(bytes(raw)), raw[-1])


if __name__ == "__main__":
    unittest.main(verbosity=2)
