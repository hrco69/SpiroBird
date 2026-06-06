"""
SpiroBird — breath scenario simulator (run BEFORE flashing hardware).

Drives the 1:1 Python port of the Controller logic with realistic ADC
sequences and prints every state transition, exactly like the firmware's
Serial output. Also writes sample_packets.json (40 Hz packet stream of the
success scenario) for the browser preview (flappy_preview.html).

Usage:
    cd sim
    python simulate_breath.py
Exit code 0 = every scenario behaved as expected.
"""

import json
import math
import random
import sys

from spiro_model import (
    POT_CENTER_ADC, POT_CENTER_HOLD_MS, SENSOR_SAMPLE_INTERVAL_MS,
    STABLE_SUCCESS_MS,
    STATE_NAMES, STATE_ACTIVE, STATE_RESULT,
    FAIL_OVER_1200, FAIL_TIMEOUT,
    FLOW_TARGET_MIN_ML_S, FLOW_TARGET_MAX_ML_S,
    BreathSensor, ExerciseLogic, flow_to_adc,
)

OFFSET = POT_CENTER_ADC   # zero-flow point is fixed at 2000 by design
TICK = SENSOR_SAMPLE_INTERVAL_MS
PACKET_EVERY_MS = 25            # 40 Hz, like ESPNOW_SEND_INTERVAL_MS
rng = random.Random(42)         # deterministic runs


def run_scenario(name, flow_fn, max_ms, attempt_timeout_ms=60000,
                 record_packets=False):
    """flow_fn(t_ms) -> target flow in ml/s. Returns (logic, packets)."""
    print(f"\n=== Scenario: {name} ===")
    sensor = BreathSensor()
    logic = ExerciseLogic(sensor, attempt_timeout_ms=attempt_timeout_ms)
    logic.request_start(0)

    packets = []
    printed = 0
    t = 0
    while t < max_ms:
        t += TICK
        # During calibration the user keeps the knob centered (as instructed).
        flow = 0.0 if logic.state != STATE_ACTIVE and t <= POT_CENTER_HOLD_MS + 200 \
            else flow_fn(t)
        noise = rng.uniform(-12, 12)   # a little ADC noise, always
        sensor.update(t, flow_to_adc(max(0.0, flow), OFFSET) + int(noise))
        logic.update(t)

        while printed < len(logic.transitions):
            ms, a, b = logic.transitions[printed]
            print(f"  [{ms:6d} ms] {a} -> {b}")
            printed += 1

        if record_packets and t % PACKET_EVERY_MS == 0:
            packets.append({
                "timestampMs": t,
                "flowMlS": round(sensor.flow_ml_s, 1),
                "filteredFlowMlS": round(sensor.filtered_flow_ml_s, 1),
                "volumeMl": round(logic.volume_ml, 1),
                "stableTimeMs": logic.stable_time_ms,
                "state": logic.state,
                "stateName": STATE_NAMES[logic.state],
                "targetZone": sensor.in_target_zone(),
                "dangerZone": sensor.in_danger_zone(),
            })

        if logic.state == STATE_RESULT:
            break

    r = logic.result
    if r.valid:
        print(f"  result: {'SUCCESS' if r.success else 'FAIL'}"
              f"  volume={r.volume_ml:.0f} ml  max={r.max_flow_ml_s:.0f}"
              f"  avg={r.avg_flow_ml_s:.0f}  stable={r.stable_time_ms} ms"
              + ("" if r.success else f"  reason={r.fail_reason}"))
    else:
        print(f"  result: attempt still running (state={STATE_NAMES[logic.state]})")
    return logic, packets


def expect(cond, msg):
    status = "OK " if cond else "FAIL"
    print(f"  [{status}] {msg}")
    return cond


def main():
    ok = True

    # 1) Stable success around 1000 ml/s -------------------------------------
    def stable_flow(t):
        return 1000.0 + 35.0 * math.sin(t * 0.002)   # gentle wobble, in zone

    logic, packets = run_scenario(
        "stable success ~1000 ml/s", stable_flow, 20000, record_packets=True)
    r = logic.result
    ok &= expect(r.valid and r.success, "exercise succeeds")
    ok &= expect(r.stable_time_ms == STABLE_SUCCESS_MS, "stable time = 5000 ms")
    ok &= expect(4500 < r.volume_ml < 9000, f"volume plausible ({r.volume_ml:.0f} ml)")

    # 2) Fail over 1200 ml/s ---------------------------------------------------
    def overdrive_flow(t):
        return min(1380.0, 0.5 * max(0, t - 1500))    # steady ramp past the limit

    logic, _ = run_scenario("fail: flow exceeds 1200 ml/s", overdrive_flow, 20000)
    r = logic.result
    ok &= expect(r.valid and not r.success, "exercise fails")
    ok &= expect(r.fail_reason == FAIL_OVER_1200, "reason = FAIL_OVER_1200")

    # 3) Unstable / twitchy breathing ------------------------------------------
    # Oscillates AROUND the target zone without ever crossing the hard fail
    # limits — only the peak-to-peak stability gate can reject this one.
    def twitchy_flow(t):
        return 1030.0 + 150.0 * math.sin(t * 0.011) + rng.uniform(-30, 30)

    logic, _ = run_scenario("unstable/twitchy flow (inside limits)", twitchy_flow,
                            14000, attempt_timeout_ms=10000)
    r = logic.result
    ok &= expect(not r.success, "never succeeds (stability gate works)")
    ok &= expect(r.fail_reason == FAIL_TIMEOUT,
                 "rejected by stability (timeout), not by the 1200 limit")

    # 4) Low flow (weak breath) -------------------------------------------------
    def low_flow(t):
        return 450.0   # detected (>150) but far below the 900 target

    logic, _ = run_scenario("low flow 450 ml/s", low_flow, 14000,
                            attempt_timeout_ms=8000)
    r = logic.result
    ok &= expect(r.valid and not r.success, "exercise fails")
    ok &= expect(r.fail_reason == FAIL_TIMEOUT, "reason = FAIL_TIMEOUT")

    # 5) Very low flow never even starts ----------------------------------------
    def tiny_flow(t):
        return 80.0    # below FLOW_START_THRESHOLD

    logic, _ = run_scenario("tiny flow 80 ml/s (below start threshold)",
                            tiny_flow, 6000)
    ok &= expect(not logic.result.valid and
                 STATE_NAMES[logic.state] == "STATE_READY",
                 "stays in STATE_READY, attempt never starts")

    # Write the packet stream for the browser preview ---------------------------
    with open("sample_packets.json", "w") as f:
        json.dump({
            "description": "SpiroBird simulated packet stream (stable success scenario, 40 Hz)",
            "targetZone": [FLOW_TARGET_MIN_ML_S, FLOW_TARGET_MAX_ML_S],
            "packets": packets,
        }, f, indent=1)
    print(f"\nWrote sample_packets.json ({len(packets)} packets)")

    print("\n" + ("ALL SCENARIOS PASSED" if ok else "SOME SCENARIOS FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
