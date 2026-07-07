"""Verify the three fixes: flash cache populated early, live hour meter, and
FuelOpen driving the (now-enabled) fuel solenoid."""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t

# ── 1. flash telemetry not 0/0 ──────────────────────────────────────
d = dut.data()
print("[flash] free=%s used=%s total=%s KB" % (d.get("flash_free_kb"), d.get("flash_used_kb"), d.get("flash_total_kb")))
print("  flash populated (not 0):", (d.get("flash_free_kb") or 0) > 0 and (d.get("flash_total_kb") or 0) > 0)

# ── 2. FuelOpen drives the fuel solenoid ────────────────────────────
dut.ensure_mode_standby(); dc.only_safety()
dc.set_sequence(startup=["FuelOpen","TimedDelay"], startup_delays=[0,5000])
dut.ensure_dev_mode(True)
if dut.data().get("bench_mode"): dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.4)
print("\n[FuelOpen] fuel_sol enabled:", dut.hardware()["actuators"]["fuel_sol"]["enabled"])
t.set("N1", hz(20000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(120); time.sleep(0.6)
dut.ensure_mode_standby()
for _ in range(8):
    c,r = dut.start()
    if c==200 or "reboot" not in str(r.get("error","")).lower(): break
    time.sleep(2)
seen_fs=False; seen_pin=False; t0=time.time()
while time.time()-t0 < 6:
    dd=dut.data()
    if dd.get("fuel_sol_open"): seen_fs=True
    if t.get("FUEL_SOL").get("level")==1: seen_pin=True
    if dd.get("mode") not in ("STARTUP","RUNNING"): break
    time.sleep(0.1)
print("  fuel_sol_open telem:", seen_fs, "| tester FUEL_SOL pin high:", seen_pin)
dut.stop(); dut.ensure_mode_standby()

# ── 3. live hour meter during a real run ────────────────────────────
dc.set_sequence(startup=["OilPumpOn","TimedDelay","IgniterOn","FuelPumpIdle","TimedDelay","IgniterOff","TimedDelay"],
                startup_delays=[0,400,0,0,400,0,400])
# real: dev off, bench off
if dut.data().get("bench_mode"): dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.3)
if dut.data().get("dev_mode"):  dut.command("TOGGLE_DEV_MODE"); time.sleep(0.3)
print("\n[hour meter] dev=%s bench=%s" % (dut.data().get("dev_mode"), dut.data().get("bench_mode")))
t.set("N1", hz(45000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200); time.sleep(0.6)
dut.ensure_mode_standby()
for _ in range(8):
    c,r = dut.start()
    if c==200 or "reboot" not in str(r.get("error","")).lower(): break
    time.sleep(2)
dut.poll_until(lambda x: x.get("mode")=="RUNNING", timeout=20)
samples=[]; t0=time.time()
while time.time()-t0 < 8:
    t.set("N1", hz(45000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200)
    samples.append(dut.data().get("total_run_seconds")); time.sleep(1.0)
print("  total_run_seconds live samples:", samples)
uniq = sorted(set(s for s in samples if s is not None))
print("  ticks up live during run:", len(uniq) >= 3, "(distinct values: %s)" % uniq)
dut.stop(); dut.ensure_mode_standby()
t.set("N1", 0)
rig.close()
