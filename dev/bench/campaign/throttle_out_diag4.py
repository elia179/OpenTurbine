"""Endpoint isolation for the servo-capture failure. cluster_serial is off, so
GPIO17 is free. Drive the DUT throttle as LEDC steady-high on GPIO17 (wired to
tester GPIO19 = STARTER_OUT) and sample that pin:
  level=1 seen on GPIO19 -> tester GPIO19 works; tester GPIO18 is the dead pin
                            (move the throttle capture to a good pin).
  nothing on GPIO19 too  -> neither tester servo-capture pin reads, or the DUT
                            S3 isn't driving GPIO16/17 (board/firmware).
Restores throttle to servo/type0/GPIO16 at the end.
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
        if code != 200: time.sleep(3); continue
        for _ in range(30):
            try: dut.status(); time.sleep(0.4)
            except Exception: break
        for _ in range(60):
            try: dut.status(); time.sleep(2.0); break
            except Exception: time.sleep(1)
        if check(dut.hardware()): return True
    return False

def sample(sig, n=200):
    hi = us_hits = 0; last_us = 0
    for _ in range(n):
        s = t.get(sig)
        if s.get("level") == 1: hi += 1
        if s.get("us", 0) > 0: us_hits += 1; last_us = s["us"]
        time.sleep(0.01)
    return hi, us_hits, last_us

print("settled:", dut.mode())
print("throttle -> LEDC on GPIO17 (tester GPIO19):",
      hard_post(lambda hw: hw["actuators"]["throttle"].update(type=1, pin=17),
                lambda hw: hw["actuators"]["throttle"]["type"] == 1 and hw["actuators"]["throttle"]["pin"] == 17))
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 500}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
rig.baseline(); rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
t.set("THROTTLE_IN", 3.3); time.sleep(2.0); t.set("N1", N1_HOLD)
eff = dut.data().get("throttle_effective")
hi19, us19, lu19 = sample("STARTER_OUT", 200)
print("  GPIO17->tester19: RUNNING=%s eff=%.3f | level=1 on %d/200 | us>0 on %d/200 (last=%d)" % (r, eff, hi19, us19, lu19))
print("\nVERDICT:", "tester GPIO19 reads it -> tester GPIO18 is the dead pin" if (hi19 or us19)
      else "neither servo-capture pin reads (both tester pins, or DUT GPIO16/17 output, at fault)")
rig.recover()
print("restore throttle servo/GPIO16:",
      hard_post(lambda hw: hw["actuators"]["throttle"].update(type=0, pin=16),
                lambda hw: hw["actuators"]["throttle"]["type"] == 0 and hw["actuators"]["throttle"]["pin"] == 16))
rig.close()
