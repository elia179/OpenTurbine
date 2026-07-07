"""Decisive: is the THROTTLE_OUT no-pulse a servo-generation fault or a dead
GPIO16->tester18 link?

Reconfigure the DUT throttle actuator to LEDC type (10 kHz) on the SAME GPIO16.
At full throttle that is ~100% duty (steady high). Read tester GPIO18:
  level=1 / us>0 seen -> wire + tester pin WORK; the servo (type 0) output is the
                         fault (ServoActuator not emitting on the S3).
  still nothing        -> GPIO16 -> tester GPIO18 link is dead (wire or tester pin).
Restores throttle to servo (type 0) at the end.
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

def sample(n=200):
    hi = us_hits = 0; last_us = 0
    for _ in range(n):
        s = t.get("THROTTLE_OUT")
        if s.get("level") == 1: hi += 1
        if s.get("us", 0) > 0: us_hits += 1; last_us = s["us"]
        time.sleep(0.01)
    return hi, us_hits, last_us

print("settled:", dut.mode())
print("throttle -> LEDC type on GPIO16:",
      hard_post(lambda hw: hw["actuators"]["throttle"].update(type=1, pin=16),
                lambda hw: hw["actuators"]["throttle"]["type"] == 1 and hw["actuators"]["throttle"]["pin"] == 16))
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 600}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
rig.baseline(); rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
t.set("THROTTLE_IN", 3.3); time.sleep(2.0); t.set("N1", N1_HOLD)
d = dut.data()
hi, us_hits, last_us = sample(200)
print("  LEDC full throttle: RUNNING=%s throttle_effective=%.3f | level=1 on %d/200 | us>0 on %d/200 (last us=%d)"
      % (r, d.get("throttle_effective"), hi, us_hits, last_us))
print("\nVERDICT:", "WIRE+TESTER-PIN OK -> servo-generation is the fault" if (hi > 0 or us_hits > 0)
      else "GPIO16 -> tester GPIO18 link is DEAD (wire or tester pin)")
rig.recover()
print("restore throttle -> servo type 0:",
      hard_post(lambda hw: hw["actuators"]["throttle"].update(type=0, pin=16),
                lambda hw: hw["actuators"]["throttle"]["type"] == 0 and hw["actuators"]["throttle"]["pin"] == 16))
rig.summary("THROTTLE_OUT LEDC discriminator")
rig.close()
