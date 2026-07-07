"""Probe steady-state actuator drive in RUNNING after a realistic sequence, and
answer: (a) is the oil pump driven with a sensor but the P-loop OFF vs ON, and
(b) does FuelOpen's fuel solenoid stay open in RUNNING. Polls DUT telemetry (fast)
+ tester pins."""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

OILC = round(10.0/4095.0, 7)
rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
SEQ = ["OilPrime","PreIgnSpark","FuelOpen","FlameConfirm","Spool"]

def run_once(oil_loop):
    dut.ensure_mode_standby()
    dc.set_controllers(oil_loop=oil_loop)
    dc.only_safety()
    dc.set_sequence(startup=SEQ, startup_delays=[0,600,0,0,0])
    dc.patch_cfg({"calibration": {"oil_poly": {"a":0,"b":0,"c":OILC,"d":0,"x_min":0,"x_max":4095}},
                  "oil": {"startup_min_bar": 1.5, "running_min": 3.0},
                  "sequence": {"spool_rpm_target": 30000}})
    dut.ensure_dev_mode(True)
    if dut.data().get("bench_mode"): dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.4)
    t.set("N1", hz(0)); t.set("OILP", 2.5); t.set("FLAME", 1); t.set_tot(120); time.sleep(0.8)
    dut.ensure_mode_standby()
    for _ in range(8):
        c, r = dut.start()
        if c == 200 or "reboot" not in str(r.get("error","")).lower(): break
        time.sleep(2)
    # high-rate telemetry probe through the sequence
    blocks=[]; fuel_seen=False; end=time.time()+22
    while time.time()-end < 0:
        d = dut.data(); m=d.get("mode"); blk=d.get("current_block")
        if blk and (not blocks or blocks[-1]!=blk): blocks.append(blk)
        if d.get("fuel_sol_open"): fuel_seen=True
        if blk=="Spool" or m=="RUNNING": t.set("N1", hz(32000))
        t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(120)
        if m=="RUNNING": break
        time.sleep(0.03)
    # settle in RUNNING, then read steady state
    for _ in range(10): t.set("N1", hz(45000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(120); time.sleep(0.2)
    d = dut.data()
    oilp_tester = t.get("OILPUMP_OUT"); fuel_tester = t.get("FUEL_SOL")
    print("  oil_loop=%s blocks=%s" % (oil_loop, blocks))
    print("    RUNNING steady: oil_pct(telem)=%s tester_oil_duty=%.2f | fuel_sol_open(telem)=%s tester_fuel_lvl=%s | fuel_seen_during_seq=%s"
          % (d.get("oil_pct"), oilp_tester.get("duty",0), d.get("fuel_sol_open"), fuel_tester.get("level"), fuel_seen))
    dut.stop(); dut.ensure_mode_standby()

print("== realistic sequence, oil P-loop OFF ==")
run_once(False)
print("== realistic sequence, oil P-loop ON ==")
run_once(True)

rig.recover()
dc.set_controllers(oil_loop=False)
dc.set_sequence(startup=["OilPumpOn","TimedDelay","IgniterOn","FuelPumpIdle","TimedDelay","IgniterOff","TimedDelay"],
                startup_delays=[0,400,0,0,400,0,400])
t.set("N1", 0)
rig.close()
