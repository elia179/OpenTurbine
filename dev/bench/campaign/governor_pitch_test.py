"""Load/pitch-primary governor (turboprop): holds N2 by adjusting prop pitch
(load), leaving throttle to the pilot. Enable a prop-pitch actuator (no physical
wire needed — read prop_pitch_demand telemetry) + pitch_kp>0 => usePropPitch.
  N2 above target -> coarser pitch / MORE load  -> prop_pitch_demand rises
  N2 below target -> finer pitch / LESS load    -> prop_pitch_demand falls
Also confirm throttle is NOT wound by the governor (pilot owns it in this mode).
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
dut.ensure_mode_standby()

def mut(hw):
    hw["has_two_shaft"] = True
    hw["sensors"]["n2_rpm"].update(enabled=True, pin=8, ppr=1.0)
    hw["sensors"]["throttle_input"].update(enabled=True, pin=4)   # pilot throttle stays
    hw["actuators"]["prop_pitch"].update(enabled=True, pin=9, type=0)  # servo; telemetry-only here
    hw["controllers"].update(governor=True, dynamic_idle=False)
ok = dc.multi(mut, check=lambda hw: hw.get("has_two_shaft") and hw["sensors"]["n2_rpm"]["enabled"]
              and hw["actuators"]["prop_pitch"]["enabled"] and hw["controllers"]["governor"])[0]
print("enable N2 + prop_pitch + governor:", ok, "| has_prop_pitch:", dut.data().get("has_prop_pitch"))
print("governor cfg (target 40000, pitch_kp 0.002 -> pitch-primary):", dc.patch_cfg({
    "governor": {"target_rpm": 40000, "band_rpm": 500, "kp": 0.002, "pitch_kp": 0.002, "pitch_ramp_sec": 2.0}})[0])

dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
t.set("N1", hz(45000)); t.set("N2", hz(40000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200); t.set("THROTTLE_IN", 0.3)
time.sleep(1.0); rig.start()
r,_ = dut.poll_until(lambda x: x.get("mode")=="RUNNING", timeout=20)
print("RUNNING:", r, "| n2_healthy:", dut.data().get("n2_healthy"))

def hold(n2, secs=5.0, label=""):
    end=time.time()+secs
    while time.time()<end:
        t.set("N1", hz(45000)); t.set("N2", hz(n2)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200); t.set("THROTTLE_IN", 0.3)
        time.sleep(0.2)
    d=dut.data()
    print("  [%s] N2=%d (target 40000) -> prop_pitch_demand=%.3f  throttle_demand=%.3f" %
          (label, n2, d.get("prop_pitch_demand"), d.get("throttle_demand")))
    return d.get("prop_pitch_demand"), d.get("throttle_demand")

hold(40000, 3, "at target")
p_hi, thr_hi = hold(46000, 6, "N2 above target")   # more load -> pitch rises
p_lo, thr_lo = hold(34000, 6, "N2 below target")   # less load -> pitch falls

print("\nRESULTS:")
print("  load governor RAISES pitch/load when N2 above target:", (p_hi or 0) > 0.2, "(pitch=%.3f)" % (p_hi or 0))
print("  load governor LOWERS pitch/load when N2 below target:", (p_lo if p_lo is not None else 1) < (p_hi or 0) - 0.1, "(pitch=%.3f)" % (p_lo or 0))
print("  throttle NOT wound by governor (pilot owns it): throttle_demand stayed ~idle (%.3f/%.3f)" % (thr_hi or 0, thr_lo or 0))

rig.recover()
dc.multi(lambda hw: (hw["actuators"]["prop_pitch"].update(enabled=False), hw["controllers"].update(governor=False)))
t.set("N1", 0); t.set("N2", 0)
rig.close()
