import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench import Tester, DUT
from otbench.dutconfig import DutConfig

def hz(rpm): return round(rpm / 60.0, 1)   # ppr = 1

def start_ready(dut, tries=8):
    for _ in range(tries):
        code, resp = dut.start()
        if code == 200 or "reboot" not in str(resp.get("error", "")).lower():
            return code, resp
        time.sleep(2)
    return code, resp

dut = DUT(); dcfg = DutConfig(dut)
LIMIT = 50000

def cfgmut(hw):
    hw["safety"].update({k: False for k in hw["safety"]})
    hw["safety"]["overspeed"] = True
    hw["shutdown_delay_ms"] = [800 if d and d > 800 else d for d in hw.get("shutdown_delay_ms", [])]

print("config (overspeed only + fast cooldown)...")
dcfg.multi(cfgmut)
print("  rpm_limit patch ->", dcfg.patch_cfg({"engine": {"rpm_limit": LIMIT}}))
print("  rpm_limit =", dut.config()["engine"]["rpm_limit"])

t = Tester("COM3").open()
t.set_tot(120); t.set("N1", 0)
dut.ensure_mode_standby()

code, resp = start_ready(dut)
print("start ->", code, resp)
ok, d = dut.poll_until(lambda x: x.get("mode") in ("STARTUP", "RUNNING"), timeout=6)
print("mode after start:", d.get("mode"))

# NEGATIVE
t.set("N1", hz(49000)); time.sleep(4)
d = dut.data()
neg_ok = d.get("mode") in ("STARTUP", "RUNNING") and "OVERSPEED" not in str(d.get("fault_description") or "")
print("NEG  N1=49000: mode=%s n1=%s -> %s" % (d.get("mode"), d.get("n1"), "OK (no trip)" if neg_ok else "FALSE TRIP!"))

# POSITIVE — capture the instant it leaves the active mode, then confirm via the flight log
import json
t0 = time.time(); t.set("N1", hz(60000))
trip_mode = None; trip_fault = ""; trip_event = ""; elapsed = None
deadline = time.time() + 8
while time.time() < deadline:
    d = dut.data(); m = d.get("mode")
    if m not in ("STARTUP", "RUNNING"):
        trip_mode = m; elapsed = time.time()-t0
        trip_fault = str(d.get("fault_description") or ""); trip_event = str(d.get("last_event") or "")
        break
    time.sleep(0.04)
try:
    log_txt = json.dumps(dut._get("/api/log")).upper()
except Exception as e:
    log_txt = "(log fetch failed: %s)" % e
logged = "OVERSPEED" in log_txt or "OVER-SPEED" in log_txt
print("POS  N1=60000: left active mode after %s -> %s | event=%r fault=%r | log has OVERSPEED=%s"
      % ("%.2fs" % elapsed if elapsed else "n/a", trip_mode, trip_event, trip_fault.splitlines()[0] if trip_fault else "", logged))

t.set("N1", 0); t.set_tot("off")
dut.stop(); dut.ensure_mode_standby()
t.close()
print("done")
