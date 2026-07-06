import sys, json, time, urllib.request
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench import Tester, DUT

base = "http://192.168.4.1"
def get(p): return json.loads(urllib.request.urlopen(base+p, timeout=6).read())
def post(p, obj):
    d = json.dumps(obj).encode()
    return urllib.request.urlopen(urllib.request.Request(base+p, data=d, method="POST",
        headers={"Content-Type":"application/json"}), timeout=8).read().decode()
def wait_back(n=45):
    time.sleep(3)
    for _ in range(n):
        try: get("/api/status"); return True
        except Exception: time.sleep(1)
    return False

# --- re-apply the bench hardware config (defaults were wiped), incl TOT ---
hw = get("/api/hardware")
hw["sensors"]["n1_rpm"]["enabled"] = True
hw["sensors"]["oil_press"]["enabled"] = True
hw["sensors"]["flame"]["enabled"] = True
hw["sensors"]["throttle_input"]["pin"] = 4
hw["sensors"]["tot"]["enabled"] = True          # max6675 on clk36/cs18/miso37
hw["actuators"]["fuel_sol"]["enabled"] = True
print("reconfig ->", post("/api/hardware", hw))
assert wait_back(), "DUT did not return"
tot = get("/api/hardware")["sensors"]["tot"]
print("TOT enabled=%s chip=%s clk=%s cs=%s miso=%s\n" % (tot["enabled"], tot["chip"], tot["clk"], tot["cs"], tot["miso"]))

dut = DUT()
t = Tester("COM3").open()
dut.stop()                # tester-open glitch recovery (tot reads work in any mode)
time.sleep(1)

print("%-7s %-9s %-9s" % ("set C", "read C", "healthy"))
for T in (100, 300, 600, 900):
    t.set_tot(T)
    time.sleep(2.0)       # MAX6675: 250 ms reads, 6-sample avg (~1.5 s)
    d = get("/api/data")
    print("%-7d %-9s %-9s" % (T, d.get("tot"), d.get("tot_healthy")))

t.set_tot("open")
time.sleep(2.0)
d = get("/api/data")
print("open   -> tot=%s healthy=%s (expect unhealthy)" % (d.get("tot"), d.get("tot_healthy")))
t.set_tot("off")
t.close()
