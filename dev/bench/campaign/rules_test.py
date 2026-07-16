"""Command rules engine (non-bench real start; rules skip in bench mode).
  A. TOT > 450 C -> IGNITER on: drive temperature across threshold, observe igniter pin.
  B. hysteresis latching on rule A (hyst 30 C): on@480, still-on@430, off@410.
  C. TOT > 600 -> REQUEST_SHUTDOWN: drive TOT across threshold -> engine shuts down.
Sensor idx: TOT=1 N1_RPM=2.  Op: GT=0.  Actuator: IGNITER=9 REQUEST_SHUTDOWN=13.
mode_mask RUNNING=4.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

OILC = round(10.0/4095.0, 7)
rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
results = []
def rec(n, ok, d=""): results.append((n, ok)); print("[%s] %-30s %s" % ("PASS" if ok else "FAIL", n, d))

def setup(rules):
    dut.ensure_mode_standby()
    dc.set_sequence(startup=["OilPumpOn","TimedDelay","IgniterOn","FuelPumpIdle","TimedDelay","IgniterOff","TimedDelay"],
                    startup_delays=[0,400,0,0,400,0,400])
    dc.only_safety()  # disarm maskable safety so we can freely drive N1/TOT
    dc.patch_cfg({"rules": rules,
                  "calibration": {"oil_poly": {"a":0,"b":0,"c":OILC,"d":0,"x_min":0,"x_max":4095}}})
    dut.ensure_dev_mode(True)
    if dut.data().get("bench_mode"): dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.4)

def reach_running():
    t.set("N1", hz(40000)); t.set("OILP", 2.5); t.set("FLAME", 1); t.set_tot(120)
    time.sleep(0.8); dut.ensure_mode_standby()
    for _ in range(8):
        c, r = dut.start()
        if c == 200 or "reboot" not in str(r.get("error","")).lower(): break
        time.sleep(2)
    return dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)

def igniter_after_tot(tot, secs=2.0):
    end = time.time() + secs; lvl = 0
    while time.time() < end:
        t.set("N1", hz(40000)); t.set("OILP", 2.5); t.set("FLAME", 1); t.set_tot(tot)
        lvl = t.get("IGNITER").get("level", 0)
        time.sleep(0.15)
    return lvl, dut.data().get("mode")

# ── A + B: igniter rule with hysteresis ─────────────────────────────
print("-- rule: TOT>450 -> IGNITER (hyst 30) --")
setup([{"enabled": True, "kind": 0, "source": "tot_main", "target": "igniter_main", "op": 0, "threshold": 450,
        "on_value": 1.0, "off_value": 0.0, "hysteresis": 30, "mode_mask": 4, "name": "ign_on_tot"}])
r, _ = reach_running(); print("  reached RUNNING:", r)
lvl_lo, _ = igniter_after_tot(400); rec("igniter OFF below threshold (TOT=400)", lvl_lo == 0, "lvl=%s" % lvl_lo)
lvl_hi, _ = igniter_after_tot(480); rec("igniter ON above threshold (TOT=480)", lvl_hi == 1, "lvl=%s" % lvl_hi)
lvl_h1, _ = igniter_after_tot(430); rec("hysteresis holds ON at 430 (>420)", lvl_h1 == 1, "lvl=%s" % lvl_h1)
lvl_h0, _ = igniter_after_tot(410); rec("releases below 420 (TOT=410)", lvl_h0 == 0, "lvl=%s" % lvl_h0)
dut.stop(); dut.ensure_mode_standby()

# ── C: REQUEST_SHUTDOWN rule ────────────────────────────────────────
print("\n-- rule: TOT>600 -> REQUEST_SHUTDOWN --")
setup([{"enabled": True, "kind": 0, "source": "tot_main", "target": "request_shutdown", "op": 0, "threshold": 600,
        "on_value": 1.0, "off_value": 0.0, "hysteresis": 0, "mode_mask": 4, "name": "shutdown_hot"}])
r, _ = reach_running(); print("  reached RUNNING:", r)
# safe TOT first
for _ in range(6): t.set("N1", hz(42000)); t.set_tot(300); t.set("OILP",2.5); t.set("FLAME",1); time.sleep(0.2)
mode_safe = dut.data().get("mode")
# cross threshold
end = time.time() + 6; left = False
while time.time() < end:
    t.set("N1", hz(42000)); t.set_tot(650); t.set("OILP", 2.5); t.set("FLAME", 1)
    if dut.data().get("mode") != "RUNNING": left = True; break
    time.sleep(0.2)
rec("stays RUNNING below 600 (TOT=300)", mode_safe == "RUNNING", "mode=%s" % mode_safe)
rec("rule shuts down above 600 (TOT=650)", left, "reason=%r" % ((dut.data().get("fault_description") or dut.data().get("last_event") or "")[:60]))

rig.recover()
dc.patch_cfg({"rules": []})  # clear rules
t.set("N1", 0)
npass = sum(1 for _,ok in results if ok)
print("\n=== Rules: %d/%d passed ===" % (npass, len(results)))
for n,ok in results:
    if not ok: print("  FAIL:", n)
rig.close()
raise SystemExit(0 if npass == len(results) else 1)
