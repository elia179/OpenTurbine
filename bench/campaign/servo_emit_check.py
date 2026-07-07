"""The DUT40->tester17 link is proven good (LEDC read cleanly). Now split the two
remaining causes for the servo failure: DUT ServoActuator not emitting vs tester
servo pulseIn capture.

Drive GPIO40 as a real 50 Hz SERVO (type 0) at full (=~2 ms high / 20 ms). Sample
tester17 LEVEL rapidly (~5 ms): a 50 Hz servo at full is HIGH ~10 % of the time,
so level should read 1 on a chunk of samples IF the DUT emits the pulse.
  level=1 on some samples -> DUT servo emits; tester servo pulseIn is the bug.
  level=0 on all samples  -> DUT ServoActuator is not emitting on this pin.
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

print("throttle -> GPIO40 SERVO(type0):",
      hard_post(lambda hw: hw["actuators"]["throttle"].update(type=0, pin=40, min_us=1000, max_us=2000),
                lambda hw: hw["actuators"]["throttle"]["type"] == 0 and hw["actuators"]["throttle"]["pin"] == 40))
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 500}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
rig.baseline(); rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
t.set("THROTTLE_IN", 3.3); time.sleep(2.0); t.set("N1", N1_HOLD)
eff = dut.data().get("throttle_effective")
hi = 0; usread = None
for _ in range(300):
    s = t.get("THROTTLE_OUT")
    if s.get("level") == 1: hi += 1
    if s.get("us", 0) > 0: usread = s
    time.sleep(0.005)
print("  RUNNING=%s eff=%.3f (servo full ~2ms/20ms=10%% high) | tester17 level=1 %d/300 | pulseIn=%s"
      % (r, eff, hi, usread))
print("\nVERDICT:", "DUT SERVO EMITS -> tester servo pulseIn capture is the bug"
      if hi > 5 else "DUT ServoActuator NOT emitting on this pin")
rig.recover()
rig.close()
