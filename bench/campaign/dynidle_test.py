"""Dynamic idle controller: raises the throttleDemand floor to hold target N1.

On the bench N1 doesn't respond to throttle (tester drives it directly), so with
the throttle input at 0 the loop winds the floor UP when N1<target and DOWN when
N1>target, and disengages above rpmLimit. Observed via throttle_effective.
Runs in bench mode (safety skipped; DynamicIdle itself is not bench-gated).
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
dut.ensure_mode_standby()

# enable the dynamic idle controller in hardware + set fast, observable gains
print("enable dynamic_idle controller:", dc.set_controllers(dynamic_idle=True)[0])
print("cfg:", dc.patch_cfg({
    "dynamic_idle": {"target_rpm": 44000, "ramp_up_ms": 2000, "ramp_down_ms": 2000,
                     "rpm_limit": 60000, "deadband_rpm": 300, "i_gain": 0},
    "throttle": {"idle_min_pct": 5, "idle_max_pct": 50, "ramp_up_ms": 300, "ramp_down_ms": 300}})[0])

dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
# turn dynamic idle ON (EngineData flag)
if not dut.data().get("dynamic_idle_enabled"): dut.command("TOGGLE_DYNAMIC_IDLE")
time.sleep(0.3)
print("dynamic_idle_enabled:", dut.data().get("dynamic_idle_enabled"), "target=", dut.data().get("idle_target_rpm"))

t.set("N1", hz(40000)); t.set_tot(300); t.set("OILP", 2.5); t.set("FLAME", 1); t.set("THROTTLE_IN", 0.0)
rig.start()
r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
print("RUNNING=%s" % r)

def hold_and_read(n1, secs=4.0, label=""):
    t.set("N1", hz(n1)); t.set("THROTTLE_IN", 0.0)
    end = time.time() + secs; eff = None
    while time.time() < end:
        t.set("N1", hz(n1))
        eff = dut.data().get("throttle_effective")
        time.sleep(0.25)
    print("  [%s] N1=%d target=44000 -> throttle_effective(floor)=%.3f" % (label, n1, eff))
    return eff

eff_low  = hold_and_read(35000, 4.0, "below target")   # error + -> floor winds UP
eff_high = hold_and_read(52000, 4.0, "above target")   # error - -> floor winds DOWN
eff_dis  = hold_and_read(62000, 3.0, "above rpm_limit")# disengage -> floor 0

print("\nRESULTS:")
print("  floor rises when N1<target:", eff_low > 0.2, "(eff=%.3f)" % eff_low)
print("  floor drops when N1>target:", eff_high < eff_low - 0.1, "(eff=%.3f)" % eff_high)
print("  disengages above rpm_limit:", eff_dis < 0.1, "(eff=%.3f)" % eff_dis)

# ── RESET_PEAKS: establish a real peak, then clear it ──────────────
t.set("N1", hz(48000)); time.sleep(1.5)
max_before = dut.data().get("max_n1")
dut.command("RESET_PEAKS"); time.sleep(0.5)
max_after = dut.data().get("max_n1")
print("\nRESET_PEAKS: max_n1 %s -> %s  (%s)" % (
    max_before, max_after,
    "PASS" if (max_before or 0) > 1000 and (max_after or 0) < (max_before or 0) * 0.5 else "CHECK"))

rig.recover()
dc.set_controllers(dynamic_idle=False)
t.set("N1", 0)
rig.close()
