import sys, json, time, math, urllib.request, statistics
sys.path.insert(0, r"C:\Users\elial\Documents\Dev\OpenTurbine\bench\harness")
from otbench import Tester

base = "http://192.168.4.1"
def get(p): return json.loads(urllib.request.urlopen(base+p, timeout=6).read())
def ntc_volts(T, rF=10000.0, r0=10000.0, t0=25.0, beta=3950.0):
    r = r0 * math.exp(beta*(1.0/(T+273.15) - 1.0/(t0+273.15)))
    return (4095.0 * r/(rF+r))/4095.0*3.3

hw = get("/api/hardware")
ot = hw["sensors"]["oil_temp"]
print("oil_temp: enabled=%s chip=%s pin=%s  (throttle_input enabled=%s)\n"
      % (ot["enabled"], ot["chip"], ot["pin"], hw["sensors"]["throttle_input"]["enabled"]))

t = Tester("COM3").open()
t.set("THROTTLE_IN", 1.65); time.sleep(1.8)   # prime the DAC + rolling average

rows = []
for T in (25, 50, 90, 130):
    v = ntc_volts(T)
    t.set("THROTTLE_IN", round(v, 3))
    time.sleep(1.8)
    d = get("/api/data")
    g = d.get("oil_temp"); h = d.get("oil_temp_healthy")
    rows.append((T, v, g, h))
    print("set %-4d C  ->  %.3f V  ->  read %-7s C   healthy=%s" % (T, v, g, h))
t.close()

errs = [abs(g-T) for T, v, g, h in rows if isinstance(g, (int, float)) and h]
print("\nmax error %.1f C, mean %.1f C" % (max(errs), statistics.mean(errs)))
