import sys, json, time, urllib.request
sys.path.insert(0, r"C:\Users\elial\Documents\Dev\OpenTurbine\bench\harness")
from otbench import Tester

base = "http://192.168.4.1"
def get(p): return json.loads(urllib.request.urlopen(base+p, timeout=6).read())
def _send(p, obj, method):
    d = json.dumps(obj).encode()
    r = urllib.request.urlopen(urllib.request.Request(base+p, data=d, method=method,
        headers={"Content-Type":"application/json"}), timeout=8)
    return r.status, r.read().decode()
def patch(p, obj): return _send(p, obj, "PATCH")
def command(cmd): return _send("/api/command", {"cmd":cmd}, "POST")

C = round(10.0/4095.0, 7)   # linear: 0..4095 raw -> 0..10 bar
newpoly = {"a":0, "b":0, "c":C, "d":0, "x_min":0, "x_max":4095}

print("1) SAVE calibration (PATCH /api/config calibration.oil_poly)")
print("   patch ->", patch("/api/config", {"calibration":{"oil_poly":newpoly}}))

print("2) READ IT BACK (persistence check)")
back = get("/api/config")["calibration"]["oil_poly"]
print("   stored oil_poly =", back)
persisted = abs(back["c"] - C) < 1e-6

print("3) drive the oil DAC and verify the saved cal is applied to the reading")
t = Tester("COM3").open()
rows = []
for volts in (0.825, 1.650, 2.475):
    t.set("OILP", volts)
    time.sleep(1.3)
    d = get("/api/data")
    raw, bar = d["oil_raw"], d["oil"]
    expect = C * raw
    rows.append((volts, raw, bar, expect))
    print("   OILP %.3f V -> raw %-4d -> oil %.2f bar (expect %.2f)" % (volts, raw, bar, expect))
t.close()

print("4) RESTORE original calibration (oil_poly = 0)")
print("   patch ->", patch("/api/config", {"calibration":{"oil_poly":{"a":0,"b":0,"c":0,"d":0,"x_min":0,"x_max":4095}}}))

func_ok = all(abs(bar - expect) < 0.25 for _, _, bar, expect in rows)
print("\nRESULT: calibration persisted=%s, applied-to-readings=%s" % (persisted, func_ok))
