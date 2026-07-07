"""Discriminate 'DUT not emitting the servo pulse' from 'tester pulseIn capture
corrupted' on the THROTTLE_OUT path (DUT GPIO16 -> tester GPIO18).

The tester's PWM_IN_SERVO read is a 30 ms blocking pulseIn(); the tester also runs
two attachInterrupt ISRs for MAX6675 TOT emulation that fire whenever the DUT
clocks the thermocouple. A long pulseIn window is easily corrupted by that ISR
storm (short LEDC capture and single digitalRead are not).

Test: at full throttle, (a) sample the pin LEVEL rapidly via the tester's GET
(digitalRead is ISR-robust) -- a 50 Hz servo at high throttle is HIGH ~10% of the
time, so level should read 1 on some samples if the signal reaches the pin; and
(b) repeat with the DUT NOT reading the thermocouple (no set_tot), to see if
pulseIn recovers. Level always 0 -> no signal on the pin (wire/emit). Level
sometimes 1 -> signal present, pulseIn is the problem.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

N1_HOLD = hz(45000)
rig = BenchRig(); dut = rig.dut; t = rig.t

def hard_post(mutate, check):
    for _ in range(4):
        dut.ensure_mode_standby()
        for _ in range(20):
            if not dut.data().get("extra_cooldown_active"): break
            time.sleep(1)
        hw = dut.hardware(); mutate(hw)
        code, resp = dut._post("/api/hardware", hw)
        if code != 200:
            print("  POST %s %s" % (code, resp)); time.sleep(3); continue
        for _ in range(30):
            try: dut.status(); time.sleep(0.4)
            except Exception: break
        for _ in range(60):
            try: dut.status(); time.sleep(2.0); break
            except Exception: time.sleep(1)
        if check(dut.hardware()): return True
    return False

print("settled:", dut.mode())
print("restore throttle -> GPIO16:", hard_post(lambda hw: hw["actuators"]["throttle"].update(pin=16),
                                               lambda hw: hw["actuators"]["throttle"]["pin"] == 16))
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 600, "ramp_down_ms": 600}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)

def sample_level(n=200):
    hi = 0; us_hits = 0; last_us = 0
    for _ in range(n):
        s = t.get("THROTTLE_OUT")
        if s.get("level") == 1: hi += 1
        if s.get("us", 0) > 0: us_hits += 1; last_us = s["us"]
        time.sleep(0.01)
    return hi, us_hits, last_us

def run(with_tot):
    tag = "TOT active" if with_tot else "TOT idle"
    if with_tot:
        rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set_tot(400), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
    else:
        rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
    rig.baseline(); rig.start()
    r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
    t.set("THROTTLE_IN", 3.3); time.sleep(2.0); t.set("N1", N1_HOLD)
    d = dut.data()
    hi, us_hits, last_us = sample_level(200)
    print("  [%s] RUNNING=%s throttle_effective=%.3f | level=1 on %d/200 | us>0 on %d/200 (last us=%d)"
          % (tag, r, d.get("throttle_effective"), hi, us_hits, last_us))
    rig.recover()

print("\n-- full throttle, sampling tester GPIO18 --")
run(with_tot=True)
run(with_tot=False)
rig.summary("THROTTLE_OUT connectivity diag")
rig.close()
