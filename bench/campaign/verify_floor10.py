"""Set fuel_pump_min_pct=10 and verify it saves + becomes the RUNNING floor."""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.dut import DUT
from otbench.tester import Tester
from otbench.benchrig import hz

d = DUT(); t = Tester(os.environ.get("OTBENCH_PORT","COM3")).open()
d.ensure_mode_standby(); d.ensure_dev_mode(True)
d.patch("/api/config", {"throttle": {"fuel_pump_min_pct": 10}}); time.sleep(0.5)
dd = d.data()
print("set 10%%: telem fuel_pump_min_pct=%s  cfg=%s" % (dd.get("fuel_pump_min_pct"), d.config()["throttle"].get("fuel_pump_min_pct")))

if not d.data().get("bench_mode"):
    if not d.data().get("dev_mode"): d.command("TOGGLE_DEV_MODE"); time.sleep(0.3)
    d.command("TOGGLE_BENCH_MODE"); time.sleep(0.3)
t.set("N1", hz(45000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200); t.set("THROTTLE_IN",0.0)
time.sleep(0.6); d.ensure_mode_standby()
for _ in range(8):
    c,r = d.start()
    if c==200 or "reboot" not in str(r.get("error","")).lower(): break
    time.sleep(2)
d.poll_until(lambda x: x.get("mode")=="RUNNING", timeout=20)
for _ in range(12): t.set("N1", hz(45000)); t.set("THROTTLE_IN",0.0); time.sleep(0.2)
eff = d.data().get("throttle_effective")
print("RUNNING at idle -> throttle_effective=%.3f (expect ~0.10 fuel floor)" % (eff or 0))
print("floor holds at 10%%:", eff is not None and 0.09 < eff < 0.115)
d.stop(); d.ensure_mode_standby(); t.set("N1", 0); t.close()
