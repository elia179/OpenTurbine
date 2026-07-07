"""Oil pressure loop: the integrating regulator drives oilPumpPct to hold
oilPressure at the target. It SKIPS in bench mode, so this uses a real (non-bench)
start with maskable safety disarmed (so driving oil low doesn't trip low-oil/
oil-zero). N1 held >min_rpm (under-speed is always-on) and FLAME present.

On the bench oil pressure is fixed by the tester DAC (doesn't respond to duty),
so the loop winds oil_pct to 100 when pressure<target and down to minPct when
pressure>target — that sign response is what we assert.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

OILC = round(10.0 / 4095.0, 7)
rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
dut.ensure_mode_standby()

print("enable oil_loop controller:", dc.set_controllers(oil_loop=True)[0])
print("minimal fast seq:", dc.set_sequence(
    startup=["OilPumpOn","TimedDelay","IgniterOn","FuelPumpIdle","TimedDelay","IgniterOff","TimedDelay"],
    startup_delays=[0,400,0,0,400,0,400])[0])
print("disarm maskable safety:", dc.only_safety()[0])
print("cfg (oil cal linear, target 3.0 bar, min 18%):", dc.patch_cfg({
    "calibration": {"oil_poly": {"a":0,"b":0,"c":OILC,"d":0,"x_min":0,"x_max":4095}},
    "oil": {"running_min": 3.0, "min_pct": 18, "use_throttle_map": False, "adjust_scale": 1.8}})[0])

# non-bench start: dev on (for live cfg), bench OFF
dut.ensure_dev_mode(True)
if dut.data().get("bench_mode"): dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.4)
print("bench_mode:", dut.data().get("bench_mode"), "(must be False)")

def baseline():
    t.set("N1", hz(45000)); t.set_tot(200); t.set("OILP", 2.5); t.set("FLAME", 1)
baseline(); time.sleep(1.0)
dut.ensure_mode_standby()
for _ in range(8):
    code, resp = dut.start()
    if code == 200 or "reboot" not in str(resp.get("error","")).lower(): break
    time.sleep(2)
r, d = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
print("reached RUNNING=%s  oil_demand(target)=%s bar" % (r, d.get("oil_demand")))

def drive_oil(volts, secs=5.0, label=""):
    end = time.time() + secs
    while time.time() < end:
        t.set("N1", hz(45000)); t.set("OILP", volts); t.set("FLAME", 1)
        time.sleep(0.25)
    d = dut.data()
    print("  [%s] OILP=%.2fV -> oil=%.2f bar (target %.2f) -> oil_pct=%s" %
          (label, volts, d.get("oil"), d.get("oil_demand"), d.get("oil_pct")))
    return d.get("oil_pct"), d.get("oil")

# above target -> duty should fall toward minPct
pct_hi, oil_hi = drive_oil(2.5, 6.0, "oil high (~6 bar > target)")
# below target -> duty should rise toward 100
pct_lo, oil_lo = drive_oil(1.0, 6.0, "oil low (~2.4 bar < target)")

print("\nRESULTS:")
print("  loop RAISES duty when pressure below target:", (pct_lo or 0) > (pct_hi or 0) + 20,
      "(pct_lo=%s pct_hi=%s)" % (pct_lo, pct_hi))
print("  duty near min when above target:", (pct_hi or 100) < 40, "(pct_hi=%s, minPct=18)" % pct_hi)

rig.recover()
dc.set_controllers(oil_loop=False)
t.set("N1", 0)
rig.close()
