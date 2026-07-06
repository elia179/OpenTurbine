import sys, os, json, time, math, urllib.request
sys.path.insert(0, r"C:\Users\elial\Documents\Dev\OpenTurbine\bench\harness")
from otbench import Tester

base = "http://192.168.4.1"
def get(p): return json.loads(urllib.request.urlopen(base+p, timeout=6).read())
def post(p, obj):
    d = json.dumps(obj).encode()
    r = urllib.request.urlopen(urllib.request.Request(base+p, data=d, method="POST",
        headers={"Content-Type":"application/json"}), timeout=8)
    return r.status, r.read().decode()
def wait_back(n=45):
    time.sleep(3)
    for _ in range(n):
        try: get("/api/status"); return True
        except Exception: time.sleep(1)
    return False
def ntc_volts(T, rF=10000.0, r0=10000.0, t0=25.0, beta=3950.0):
    r = r0 * math.exp(beta*(1.0/(T+273.15) - 1.0/(t0+273.15)))
    raw = 4095.0 * r/(rF+r)
    return raw/4095.0*3.3

# --- reconfigure: oil-temp NTC onto GPIO4, free the throttle input ---
hw = get("/api/hardware")
orig_throttle = dict(hw["sensors"]["throttle_input"])
hw["sensors"]["oil_temp"]["enabled"] = True
hw["sensors"]["oil_temp"]["chip"] = "ntc"
hw["sensors"]["oil_temp"]["pin"] = 4
hw["sensors"]["throttle_input"]["enabled"] = False
print("reconfig POST ->", post("/api/hardware", hw))
assert wait_back(), "S3 did not come back"
print("S3 back; oil_temp NTC now on GPIO4\n")

# --- drive the divider voltage on the tester DAC (wired to S3 GPIO4) ---
t = Tester("COM3").open()
print("%-7s %-8s %-9s %-8s" % ("set C", "volts", "read C", "healthy"))
rows = []
for T in (25, 50, 90, 120):
    v = ntc_volts(T)
    t.set("THROTTLE_IN", round(v, 3))
    time.sleep(1.4)
    d = get("/api/data")
    got = d.get("oil_temp"); h = d.get("oil_temp_healthy")
    rows.append((T, v, got, h))
    print("%-7d %-8.3f %-9s %-8s" % (T, v, got, h))
t.close()

err = max(abs(g-T) for T, v, g, h in rows if isinstance(g, (int, float)))
print("\nmax error: %.1f C across sweep" % err)

# save originals for restore step (do not restore yet)
json.dump({"throttle": orig_throttle},
          open(os.path.join(os.path.dirname(__file__), "ntc_restore.json"), "w"))
print("(saved restore info; throttle input still disabled for now)")
