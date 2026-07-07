"""Isolate FuelOpen: does the fuel solenoid it opens actually stay open?
Startup seq = [FuelOpen, TimedDelay(6s)] so the state is observable during the
delay. Non-bench. Polls fuel_sol_open telemetry + tester FUEL_SOL pin."""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
dut.ensure_mode_standby()
dc.only_safety()
print("seq [FuelOpen, TimedDelay]:", dc.set_sequence(startup=["FuelOpen","TimedDelay"], startup_delays=[0,6000])[0])
dut.ensure_dev_mode(True)
if dut.data().get("bench_mode"): dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.4)
print("bench:", dut.data().get("bench_mode"), "hasFuelSol pin:", dut.hardware()["actuators"]["fuel_sol"])

t.set("N1", hz(20000)); t.set("OILP", 2.5); t.set("FLAME", 1); t.set_tot(120); time.sleep(0.8)
dut.ensure_mode_standby()
for _ in range(8):
    c, r = dut.start()
    if c==200 or "reboot" not in str(r.get("error","")).lower(): break
    time.sleep(2)

t0=time.time(); rows=[]
while time.time()-t0 < 9:
    d = dut.data()
    rows.append((round(time.time()-t0,2), d.get("mode"), d.get("current_block"),
                 d.get("fuel_sol_open"), t.get("FUEL_SOL").get("level")))
    t.set("N1", hz(20000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(120)
    time.sleep(0.25)
last=None
for ti,m,blk,fs,lvl in rows:
    line="t=%4.2f mode=%-8s blk=%-12s fuel_sol_open=%s tester_fuel=%s" % (ti,m,blk,fs,lvl)
    if (m,blk,fs,lvl)!=last: print("  "+line)
    last=(m,blk,fs,lvl)
any_open = any(fs for _,_,_,fs,_ in rows)
any_pin  = any(lvl==1 for _,_,_,_,lvl in rows)
print("\nfuel_sol_open ever true:", any_open, "| tester saw fuel pin high:", any_pin)

rig.recover()
dc.set_sequence(startup=["OilPumpOn","TimedDelay","IgniterOn","FuelPumpIdle","TimedDelay","IgniterOff","TimedDelay"],
                startup_delays=[0,400,0,0,400,0,400])
t.set("N1", 0)
rig.close()
