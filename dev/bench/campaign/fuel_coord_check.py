"""Verify fuel-pump / fuel-solenoid coordination on the actuator OUTPUTS with the
FuelOpen-before-FuelPumpIdle sequence: the solenoid (tester FUEL_SOL) should open
before/with the fuel-pump-ESC throttle output (tester THROTTLE_OUT=GPIO17), and
both stay on in RUNNING. Also prints preflight seq_issues (should be no fuel-sol
warning)."""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; t = rig.t
dut.ensure_mode_standby()
iss = dut.data().get("seq_issues") or []
print("seq_issues now:")
for i in iss: print("  [error=%s] %s: %s" % (i.get("error"), i.get("block"), i.get("msg")))
print("  fuel-sol warning present:", any("solenoid" in (i.get("msg","")) for i in iss))

dut.ensure_dev_mode(True); dut.ensure_bench_mode(True)
t.set("N1", hz(0)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(120); time.sleep(0.6)
dut.ensure_mode_standby()
for _ in range(8):
    c,r = dut.start()
    if c==200 or "reboot" not in str(r.get("error","")).lower(): break
    time.sleep(2)
print("\ntimeline (fuel pump ESC output = tester THROTTLE_OUT GPIO17; solenoid = tester FUEL_SOL):")
t0=time.time(); rows=[]; last=None
while time.time()-t0 < 8:
    d=dut.data(); m=d.get("mode"); blk=d.get("current_block")
    thr=t.get("THROTTLE_OUT"); fs=t.get("FUEL_SOL")
    row=(m, blk, round(thr.get("us",0)), fs.get("level"), d.get("fuel_sol_open"))
    if row!=last:
        print("  t=%4.2f mode=%-8s blk=%-12s pumpESC=%4dus solenoid=%s (fw fuel_sol_open=%s)" %
              (time.time()-t0, m, blk, row[2], row[3], row[4]))
        last=row
    if m=="RUNNING": rows.append(row)
    if m not in ("STARTUP","RUNNING") and time.time()-t0>2: break
    time.sleep(0.1)
# steady RUNNING check
for _ in range(6): t.set("N1", hz(45000)); time.sleep(0.2)
d=dut.data(); thr=t.get("THROTTLE_OUT"); fs=t.get("FUEL_SOL")
print("\nRUNNING steady: pumpESC=%dus solenoid_level=%s fuel_sol_open=%s throttle_eff=%.3f" %
      (thr.get("us",0), fs.get("level"), d.get("fuel_sol_open"), d.get("throttle_effective")))
print("coordinated (pump>idle AND solenoid open):",
      thr.get("us",0) > 1000 and fs.get("level")==1)
dut.stop(); dut.ensure_mode_standby(); t.set("N1",0)
rig.close()
