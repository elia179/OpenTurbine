"""Validate the THROTTLE_OUT servo path (DUT GPIO16 -> tester GPIO18).

The handoff flagged the tester read no pulse on the throttle ESC output; the
original suite only tested throttle *input*. In STANDBY all ESC outputs are gated
off (us=0), so the servo must be tested with the engine actually driven: bench
start -> RUNNING, then sweep the throttle input and confirm the tester reads a
1000-2000 us @ 50 Hz servo pulse that tracks throttle_effective.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

N1_HOLD = hz(45000)
rig = BenchRig(); dut = rig.dut; t = rig.t
print("settled:", dut.mode())

# fast throttle slew so the sweep settles quickly
print("fast slew:", rig.dcfg.patch_cfg({"throttle": {"ramp_up_ms": 800, "ramp_down_ms": 800}})[0])
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)

rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set_tot(400), t.set("OILP", 2.5), t.set("FLAME", 1), t.set("THROTTLE_IN", 0.0))
rig.baseline(); rig.start()
r, d = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
print("reached RUNNING:", r)

def read_throttle_out(label, secs=3.0):
    t.set("N1", N1_HOLD)
    best = None
    end = time.time() + secs
    while time.time() < end:
        s = t.get("THROTTLE_OUT")
        if s.get("us", 0) > 0:
            best = s
        time.sleep(0.1)
    d = dut.data()
    print("  [%s] tester THROTTLE_OUT=%s  DUT throttle_effective=%.3f demand=%.3f"
          % (label, best, d.get("throttle_effective"), d.get("throttle_demand")))
    return best

# idle (throttle input 0)
t.set("THROTTLE_IN", 0.0); time.sleep(2.0)
lo = read_throttle_out("throttle 0%")
# full (throttle input 3.3 V)
t.set("THROTTLE_IN", 3.3); time.sleep(2.5)
hi = read_throttle_out("throttle 100%")

print("\nRESULT:")
if lo and hi and lo.get("us", 0) > 0 and hi.get("us", 0) > 0:
    print("  servo pulse present. idle=%sus full=%sus hz=%s -> %s"
          % (lo["us"], hi["us"], hi.get("hz"),
             "TRACKS demand (PASS)" if hi["us"] > lo["us"] + 100 else "PRESENT but not tracking (CHECK)"))
else:
    print("  NO PULSE on tester GPIO18 -> throttle servo wire STILL not seated (idle=%s full=%s)" % (lo, hi))

rig.recover()
rig.summary("THROTTLE_OUT servo path")
rig.close()
