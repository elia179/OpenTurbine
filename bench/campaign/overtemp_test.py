import sys, json, time, urllib.request
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench import Tester, DUT

base = "http://192.168.4.1"
def get(p): return json.loads(urllib.request.urlopen(base+p, timeout=6).read())
def _send(p, obj, m):
    d = json.dumps(obj).encode()
    return urllib.request.urlopen(urllib.request.Request(base+p, data=d, method=m,
        headers={"Content-Type":"application/json"}), timeout=8).read().decode()
def post(p, obj): return _send(p, obj, "POST")
def patch(p, obj): return _send(p, obj, "PATCH")
def wait_back(n=45):
    time.sleep(3)
    for _ in range(n):
        try: get("/api/status"); return True
        except Exception: time.sleep(1)
    return False

# 1) arm overtemp safety + set the limit
hw = get("/api/hardware"); hw["safety"]["overtemp"] = True
print("arm overtemp ->", post("/api/hardware", hw)); wait_back()
patch("/api/config", {"engine": {"tot_limit": 700}})
print("tot_limit =", get("/api/config")["engine"]["tot_limit"])

dut = DUT(); t = Tester("COM3").open()
d = dut.data()
print("flags after reboot: bench=%s dev=%s skip_safety=%s" % (d.get("bench_mode"), d.get("dev_mode"), d.get("skip_safety_checks")))
dut.ensure_mode_standby()                      # clear any tester-open START glitch
t.set_tot(120); t.set("OILP", 1.65)            # safe EGT, some oil so start is happy
time.sleep(2.0)

# 2) normal start (NOT bench mode) -> STARTUP, where safety is live
code, resp = dut.start()
print("start ->", code, resp)
ok, d = dut.poll_until(lambda x: x.get("mode") in ("STARTUP", "RUNNING"), timeout=6)
print("mode after start:", d.get("mode"), "| tot=%s" % d.get("tot"))

# 3) drive EGT past the limit and time the shutdown
print("\ndriving TOT 120 -> 800 (limit 700)...")
t0 = time.time(); t.set_tot(800)
tripped = False
for _ in range(50):
    d = dut.data()
    fault = str(d.get("fault_description") or "")
    if d.get("mode") in ("SHUTDOWN", "FAULT") or "OVERTEMP" in fault or "over-temp" in fault.lower():
        print("*** TRIPPED after %.1fs: mode=%s tot=%s" % (time.time()-t0, d.get("mode"), d.get("tot")))
        print("    fault: %s" % fault.splitlines()[0] if fault else "")
        print("    last_event: %r" % d.get("last_event"))
        tripped = True
        break
    time.sleep(0.2)
if not tripped:
    print("did NOT trip within 10s: mode=%s tot=%s" % (d.get("mode"), d.get("tot")))

# 4) cleanup: safe temp, stop, disarm overtemp
t.set_tot("off"); dut.stop(); time.sleep(1)
hw = get("/api/hardware"); hw["safety"]["overtemp"] = False; post("/api/hardware", hw)
t.close()
print("\ncleanup done (overtemp disarmed)")
