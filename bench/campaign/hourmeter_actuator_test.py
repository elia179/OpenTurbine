"""Re-enable bench actuators (fuel_sol, starter_en) and test the hour meter:
does total_run_seconds accumulate after a REAL (non-bench, non-dev) run? (It only
updates on stop, and only for non-bench/non-dev runs.)"""
import sys, time, os, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

OILC = round(10.0/4095.0, 7)
rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
dut.ensure_mode_standby()

# ── enable bench actuators ──────────────────────────────────────────
h = dut.hardware()
h["actuators"]["fuel_sol"]["enabled"] = True
h["actuators"]["fuel_sol"]["pin"] = 12
h["actuators"]["starter_en"]["enabled"] = True
h["actuators"]["starter_en"]["pin"] = 39
code, resp = dut._post("/api/hardware", h)
print("enable fuel_sol + starter_en:", code, json.dumps(resp)[:40])
if code == 200:
    for _ in range(30):
        try: dut.status(); time.sleep(0.4)
        except Exception: break
    for _ in range(60):
        try: dut.status(); time.sleep(2.0); break
        except Exception: time.sleep(1)
a = dut.hardware()["actuators"]
print("fuel_sol enabled=%s  starter_en enabled=%s" % (a["fuel_sol"]["enabled"], a["starter_en"]["enabled"]))

# ── hour meter: real (non-bench, non-dev) run ───────────────────────
# ensure NOT dev and NOT bench
if dut.data().get("bench_mode"):
    # bench toggle needs dev on; turn dev on, bench off, dev off
    if not dut.data().get("dev_mode"): dut.command("TOGGLE_DEV_MODE"); time.sleep(0.3)
    dut.command("TOGGLE_BENCH_MODE"); time.sleep(0.3)
if dut.data().get("dev_mode"): dut.command("TOGGLE_DEV_MODE"); time.sleep(0.3)
print("dev_mode=%s bench_mode=%s" % (dut.data().get("dev_mode"), dut.data().get("bench_mode")))

tot0 = dut.data().get("total_run_seconds")
# minimal timed startup reaches RUNNING on timers even non-bench
t.set("N1", hz(45000)); t.set("OILP", 2.5); t.set("FLAME", 1); t.set_tot(200); time.sleep(0.8)
dut.ensure_mode_standby()
for _ in range(8):
    c, r = dut.start()
    if c==200 or "reboot" not in str(r.get("error","")).lower(): break
    time.sleep(2)
ok, _ = dut.poll_until(lambda x: x.get("mode")=="RUNNING", timeout=20)
print("reached RUNNING:", ok)
# hold RUNNING ~14s, watch total_run_seconds live
live = []
t0 = time.time()
while time.time()-t0 < 14:
    t.set("N1", hz(45000)); t.set("OILP",2.5); t.set("FLAME",1); t.set_tot(200)
    d = dut.data()
    live.append(d.get("total_run_seconds"))
    if d.get("mode") != "RUNNING":
        print("  left RUNNING early: mode=%s reason=%r" % (d.get("mode"), (d.get("fault_description") or "")[:60])); break
    time.sleep(1.0)
print("  total_run_seconds live during run:", live[:3], "...", live[-3:])
dut.stop(); time.sleep(1.0); dut.ensure_mode_standby()
time.sleep(1.0)
tot1 = dut.data().get("total_run_seconds")
print("\ntotal_run_seconds: before=%s  after stop=%s  (delta=%s)" % (tot0, tot1, (tot1 or 0)-(tot0 or 0)))
print("live-increments-during-run:", len(set(x for x in live if x is not None)) > 1)
print("accumulates-on-stop:", (tot1 or 0) > (tot0 or 0))

t.set("N1", 0)
rig.close()
