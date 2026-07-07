"""Clean isolation: is the throttle-output failure the TESTER's servo-capture pin
(GPIO17) / wire, or the servo pulseIn software?

GPIO40 is a known-good S3 output pin. Drive it steady-high (LEDC 100%) and sample
tester GPIO17 LEVEL rapidly (digitalRead, independent of the pulseIn capture mode
and of any ISR timing).
  level=1 ever  -> DUT40->tester17 wire+pin OK; the servo pulseIn capture is the bug.
  level=0 always-> DUT40->tester17 link dead (wire not seated on 40, or tester17 pin).
Also prints the pulseIn-based read for comparison. Restores throttle servo on 40.
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

print("throttle -> GPIO40 LEDC:",
      hard_post(lambda hw: hw["actuators"]["throttle"].update(type=1, pin=40),
                lambda hw: hw["actuators"]["throttle"]["type"] == 1 and hw["actuators"]["throttle"]["pin"] == 40))
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 500}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
rig.baseline(); rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
t.set("THROTTLE_IN", 3.3); time.sleep(2.0); t.set("N1", N1_HOLD)
eff = dut.data().get("throttle_effective")
hi = 0; sample = None
for _ in range(200):
    s = t.get("THROTTLE_OUT")
    if s.get("level") == 1: hi += 1
    if s.get("us", 0) > 0: sample = s
    time.sleep(0.01)
print("  RUNNING=%s eff=%.3f (LEDC ~100%% => steady high) | tester17 level=1 %d/200 | pulseIn sample=%s"
      % (r, eff, hi, sample))
print("\nVERDICT:", "signal REACHES tester17 -> servo pulseIn capture is the bug"
      if hi > 0 else "NOTHING on tester17 -> DUT40->tester17 link dead (wire/tester pin)")
rig.recover()
hard_post(lambda hw: hw["actuators"]["throttle"].update(type=0, pin=40),
          lambda hw: hw["actuators"]["throttle"]["type"] == 0 and hw["actuators"]["throttle"]["pin"] == 40)
rig.close()
