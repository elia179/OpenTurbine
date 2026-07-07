"""Isolate the THROTTLE_OUT no-pulse fault: DUT servo generation vs the
GPIO16->tester18 wire/pin.

Move the DUT throttle ESC to GPIO17 (wired to tester GPIO19 = STARTER_OUT, the
other PWM_IN_SERVO capture channel). Drive throttle, read tester STARTER_OUT:
  pulse on GPIO19  -> DUT servo output is fine; fault is the GPIO16->tester18 path
  no pulse either  -> DUT ServoActuator isn't emitting (firmware/pin), not the wire
Restores the throttle pin to 16 at the end.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

N1_HOLD = hz(45000)
rig = BenchRig(); dut = rig.dut; t = rig.t

def hard_post(mutate, check):
    for _ in range(3):
        hw = dut.hardware(); mutate(hw)
        code, resp = dut._post("/api/hardware", hw)
        if code != 200:
            print("  POST rejected:", code, resp); time.sleep(2); continue
        for _ in range(30):
            try: dut.status(); time.sleep(0.4)
            except Exception: break
        for _ in range(60):
            try: dut.status(); time.sleep(2.0); break
            except Exception: time.sleep(1)
        if check(dut.hardware()):
            return True
    return False

print("settled:", dut.mode())
dut.ensure_mode_standby()
ok = hard_post(lambda hw: hw["actuators"]["throttle"].update(pin=17),
               lambda hw: hw["actuators"]["throttle"]["pin"] == 17)
print("throttle ESC moved to GPIO17 (verified):", ok)
rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 800, "ramp_down_ms": 800}})
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set_tot(400), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 0.0))
rig.baseline(); rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
print("reached RUNNING:", r)

def probe(label, secs=3.0):
    t.set("N1", N1_HOLD); best = None; end = time.time() + secs
    while time.time() < end:
        s = t.get("STARTER_OUT")
        if s.get("us", 0) > 0: best = s
        time.sleep(0.1)
    d = dut.data()
    print("  [%s] tester STARTER_OUT(GPIO19)=%s  throttle_effective=%.3f" % (label, best, d.get("throttle_effective")))
    return best

t.set("THROTTLE_IN", 0.0); time.sleep(2.0); lo = probe("throttle 0%")
t.set("THROTTLE_IN", 3.3); time.sleep(2.5); hi = probe("throttle 100%")

print("\nDIAGNOSIS:")
if hi and hi.get("us", 0) > 0:
    print("  DUT servo output WORKS on GPIO17->tester19 (idle=%sus full=%sus)." % (lo.get("us") if lo else None, hi["us"]))
    print("  => The no-pulse fault is specific to the GPIO16 -> tester GPIO18 path (wire or tester pin).")
else:
    print("  No pulse on GPIO17->tester19 either => DUT ServoActuator not emitting (firmware/pin), not the wire.")

rig.recover()
print("restore throttle pin -> 16:", hard_post(lambda hw: hw["actuators"]["throttle"].update(pin=16),
                                               lambda hw: hw["actuators"]["throttle"]["pin"] == 16))
rig.summary("THROTTLE_OUT swap diagnosis")
rig.close()
