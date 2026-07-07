"""Restore the full bench DUT profile after a uploadfs (which resets config to the
minimal no-sensor default). Enables all wired bench sensors + actuators to the
pinmap layout, in ONE hardware POST. Run this any time n1/oil/flame read 0 or
fuel_sol/starter_en are off."""
import sys, time, os, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.dut import DUT

d = DUT(); d.ensure_mode_standby()
h = d.hardware()
# sensors
h["sensors"]["n1_rpm"].update(enabled=True, pin=14, ppr=1.0)
h["sensors"]["oil_press"].update(enabled=True, pin=1)
h["sensors"]["flame"].update(enabled=True, pin=2)
h["sensors"]["throttle_input"].update(enabled=True, pin=4)
h["sensors"]["idle_input"].update(enabled=True, pin=5)
h["sensors"]["tot"].update(enabled=True, chip="max6675", clk=36, cs=18, miso=37)
# actuators
h["actuators"]["throttle"].update(enabled=True, type=0, pin=40)   # servo on GPIO40 (GPIO16 default; both work)
h["actuators"]["oil_pump"].update(enabled=True, pin=11)
h["actuators"]["igniter"].update(enabled=True, pin=21)
h["actuators"]["fuel_sol"].update(enabled=True, pin=12)
h["actuators"]["starter_en"].update(enabled=True, pin=39)
# coherent startup seq that OPENS the fuel solenoid (FuelOpen) alongside the fuel
# pump idle, so an enabled fuel_sol isn't left shut while the pump runs.
h["startup_seq"] = ["OilPumpOn","TimedDelay","IgniterOn","FuelOpen","FuelPumpIdle","TimedDelay","IgniterOff","TimedDelay"]
h["startup_delay_ms"] = [0,400,0,0,0,400,0,400]
code, resp = d._post("/api/hardware", h)
print("restore POST:", code, json.dumps(resp)[:50])
if code == 200:
    for _ in range(30):
        try: d.status(); time.sleep(0.4)
        except Exception: break
    for _ in range(60):
        try: d.status(); time.sleep(2.0); break
        except Exception: time.sleep(1)
hh = d.hardware()
s = hh["sensors"]; a = hh["actuators"]
print("sensors:  " + ", ".join("%s=%s"%(k, s[k]["enabled"]) for k in ("n1_rpm","oil_press","flame","tot","throttle_input","idle_input")))
print("actuators:" + ", ".join("%s=%s"%(k, a[k]["enabled"]) for k in ("throttle","oil_pump","igniter","fuel_sol","starter_en")))
