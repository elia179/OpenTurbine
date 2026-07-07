"""N2 power-turbine governor (throttle-primary): holds N2 at target by adjusting
throttleDemand. On the bench N2 is driven directly (doesn't respond), so the
governor winds throttle UP when N2<target and DOWN when N2>target. Observed via
throttle_demand. Runs in bench mode (controllers run, safety skipped)."""
import sys, time, os, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
dut.ensure_mode_standby()

# enable N2 sensor + two-shaft + governor controller (one hardware POST)
def mut(hw):
    hw["has_two_shaft"] = True
    hw["sensors"]["n2_rpm"].update(enabled=True, pin=8, ppr=1.0)
    # throttle_input stays ENABLED: a throttle-primary governor must OVERRIDE it
    # (post-fix). Its per-tick input mapping should be skipped while the governor
    # is active so the governor can accumulate to hold N2.
    hw["sensors"]["throttle_input"].update(enabled=True, pin=4)
    hw["controllers"].update(governor=True, dynamic_idle=False)
ok = dc.multi(mut, check=lambda hw: hw.get("has_two_shaft") and hw["sensors"]["n2_rpm"]["enabled"]
              and hw["controllers"]["governor"] and hw["sensors"]["throttle_input"]["enabled"])[0]
print("enable N2 + two-shaft + governor (throttle_input ENABLED - tests override):", ok)
# fuel_pump_min_pct is the ONLY running throttle floor now (idle_min_pct is just the
# idle-INPUT mapping range, not a running floor). Set the fuel floor to 8% and verify
# the throttle-driven governor cannot wind below it when N2 is above target.
print("governor cfg (target 40000, kp 0.002, fuel floor 8%):", dc.patch_cfg({
    "governor": {"target_rpm": 40000, "band_rpm": 500, "kp": 0.002},
    "throttle": {"fuel_pump_min_pct": 8}})[0])
# check N2 reads
dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
t.set("N2", hz(40000)); time.sleep(1.5)
d = dut.data()
print("N2 drive 40000 -> n2=%s n2_healthy=%s (has_n2=%s two_shaft ok)" % (d.get("n2"), d.get("n2_healthy"), d.get("has_n2")))
if not d.get("n2_healthy"):
    print("!! N2 not healthy - check tester GPIO18 -> DUT GPIO27 wire")

# reach RUNNING
t.set("N1", hz(45000)); t.set("N2", hz(40000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200)
time.sleep(0.6); rig.start()
r,_ = dut.poll_until(lambda x: x.get("mode")=="RUNNING", timeout=20)
print("RUNNING:", r)

def hold(n2, secs=4.0, label=""):
    end=time.time()+secs
    while time.time()<end:
        t.set("N1", hz(45000)); t.set("N2", hz(n2)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200); t.set("THROTTLE_IN", 0.3)
        time.sleep(0.2)
    d=dut.data()
    print("  [%s] N2=%d (target 40000) -> throttle_demand=%.3f throttle_eff=%.3f" %
          (label, n2, d.get("throttle_demand"), d.get("throttle_effective")))
    return d.get("throttle_demand")

# settle at target
hold(40000, 3, "at target")
dem_lo = hold(34000, 5, "N2 below target")   # error + -> throttle winds UP
dem_hi = hold(46000, 5, "N2 above target")   # error - -> throttle winds DOWN

lo = dem_lo if dem_lo is not None else 0.0
hi = dem_hi if dem_hi is not None else 1.0
fuel_floor = 0.08  # throttle.fuel_pump_min_pct set above
print("\nRESULTS:")
print("  governor RAISES throttle when N2 below target:", lo > 0.5, "(dem=%.3f)" % lo)
print("  governor LOWERS throttle when N2 above target:", hi < lo - 0.2, "(dem=%.3f)" % hi)
print("  respects fuel floor (>= ~8%%, not 0):", hi >= fuel_floor - 0.005, "(dem=%.3f)" % hi)

rig.recover()
dc.set_controllers(governor=False)
dc.patch_cfg({"throttle": {"fuel_pump_min_pct": 0}})
t.set("N1", 0); t.set("N2", 0)
rig.close()
