"""Definitive DUT-output test for the throttle: put the throttle actuator on the
oil pump's KNOWN-GOOD pin/wire (DUT GPIO11 -> tester GPIO21, an LEDC capture that
reads fine) and drive it. Bypasses the throttle wire and GPIO16/17 entirely.
  tester GPIO21 reads the throttle duty -> DUT throttle output logic is fine;
      GPIO16/17 are the dead pins on this S3 -> remap throttle to another S3 pin.
  nothing on GPIO21 -> deeper DUT-side output problem.
Restores oil pump on 11 and throttle servo on 16 afterwards.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

N1_HOLD = hz(45000)
rig = BenchRig(); dut = rig.dut; t = rig.t

def hard_post(mutate, check, tries=4):
    for _ in range(tries):
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

print("settled:", dut.mode())
# throttle actuator -> GPIO11 LEDC, oil pump off (free the pin/wire)
ok = hard_post(
    lambda hw: (hw["actuators"]["oil_pump"].update(enabled=False),
                hw["actuators"]["throttle"].update(enabled=True, type=1, pin=11)),
    lambda hw: hw["actuators"]["throttle"]["pin"] == 11 and hw["actuators"]["throttle"]["type"] == 1
               and not hw["actuators"]["oil_pump"]["enabled"])
print("throttle -> GPIO11 LEDC, oil pump off (verified):", ok)
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 500}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 3.3))
rig.baseline(); rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
t.set("THROTTLE_IN", 3.3); time.sleep(2.0); t.set("N1", N1_HOLD)
eff = dut.data().get("throttle_effective")
# read tester OILPUMP_OUT (GPIO21) which now carries the throttle LEDC
hi = us = 0; best = None
for _ in range(150):
    s = t.get("OILPUMP_OUT")
    if s.get("level") == 1: hi += 1
    if s.get("duty", 0) > 0.02 or s.get("us", 0) > 0: us += 1; best = s
    time.sleep(0.01)
print("  RUNNING=%s throttle_effective=%.3f | tester GPIO21 level=1 %d/150, active %d/150, sample=%s"
      % (r, eff, hi, us, best))
print("\nVERDICT:", "DUT OUTPUT OK on GPIO11 -> GPIO16/17 are the dead pins (remap throttle)"
      if (hi or us) else "no output even on GPIO11 -> deeper DUT issue")
rig.recover()
# restore: oil pump on 11 LEDC, throttle servo on 16
hard_post(lambda hw: (hw["actuators"]["oil_pump"].update(enabled=True, pin=11),
                      hw["actuators"]["throttle"].update(enabled=True, type=0, pin=16)),
          lambda hw: hw["actuators"]["oil_pump"]["enabled"] and hw["actuators"]["oil_pump"]["pin"] == 11
                     and hw["actuators"]["throttle"]["pin"] == 16 and hw["actuators"]["throttle"]["type"] == 0)
print("restored oil pump + throttle servo.")
rig.close()
