"""CLASSIC ESP32 pin-function sign-off (role-reversed: OpenTurbine on classic = DUT,
S3 = tester on COM4). Validates every pin FUNCTION the current wiring can reach.

Uses THREE small configs (one hardware POST each) — a single all-in-one config is rejected
("Invalid hardware section JSON") because the controls/start-stop-pin section must be set
coherently, which is a config-structure quirk, not a pin-function limitation:

  1. outputs : oil_pump LEDC (GPIO21), fuel_sol/igniter/starter_en (GPIO22/23/33)
  2. servo   : throttle servo (GPIO21) -> S3 reads the pulse width on OILPUMP_OUT
  3. inputs  : n1_rpm (GPIO4 freq), p1 (GPIO32 ADC1), DI channel (GPIO27 digital)

Not reachable with this wiring (documented, NOT firmware bugs): thermocouple SPI (classic
input-only pins 34/35 can't be an SPI master on the TOT jumpers) and a full analog *sweep*
(the S3 tester has no DAC, so ADC is proven at range extremes only).
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.dut import DUT
from otbench.dutconfig import DutConfig
from otbench.tester import Tester

dut = DUT(); dc = DutConfig(dut); t = Tester("COM4").open()
results = []
def rec(n, ok, d=""): results.append((n, ok)); print("[%s] %-32s %s" % ("PASS" if ok else "FAIL", n, d))
dut.ensure_mode_standby(); dut.ensure_dev_mode(True)

def disable_all(hw):
    for grp in ("sensors", "actuators"):
        for x in hw[grp].values():
            if isinstance(x, dict) and "enabled" in x: x["enabled"] = False

def watch(sig, kind, secs=2.5):
    peak = 0; act = False; end = time.time() + secs
    while time.time() < end:
        f = t.get(sig)
        if kind == "level" and f.get("level") == 1: act = True
        elif kind == "duty": peak = max(peak, f.get("duty", 0) or 0); act = act or peak > 0.05
        elif kind == "us":   peak = max(peak, f.get("us", 0) or 0);   act = act or peak > 800
        time.sleep(0.1)
    return act, peak

# ── 1. OUTPUTS: LEDC PWM + digital relays ──────────────────────────
def cfg_out(hw):
    disable_all(hw)
    hw["actuators"]["oil_pump"].update(enabled=True, pin=21)                  # LEDC -> S3 GPIO11
    hw["actuators"]["fuel_sol"].update(enabled=True, pin=22, active_h=True)
    hw["actuators"]["igniter"].update(enabled=True, pin=23, active_h=True)
    hw["actuators"]["starter_en"].update(enabled=True, pin=33, active_h=True)
dc.multi(cfg_out, check=lambda hw: hw["actuators"]["oil_pump"].get("pin") == 21)
dut.command("SET_OIL_PCT", iParam=100); a, p = watch("OILPUMP_OUT", "duty"); rec("LEDC PWM output (oil_pump)", a, "duty=%.2f" % p); dut.command("SET_OIL_PCT", iParam=0)
dut.command("FUEL_SOL_TEST");   a, _ = watch("FUEL_SOL",   "level"); rec("digital output (fuel_sol)",   a)
dut.command("IGN_TEST");        a, _ = watch("IGNITER",    "level"); rec("digital output (igniter)",    a)
dut.command("STARTER_EN_TEST"); a, _ = watch("STARTER_EN", "level"); rec("digital output (starter_en)", a)

# ── 2. SERVO output ────────────────────────────────────────────────
def cfg_servo(hw):
    disable_all(hw)
    hw["actuators"]["throttle"].update(enabled=True, pin=21, type=0, min_us=1000, max_us=2000)
dc.multi(cfg_servo, check=lambda hw: hw["actuators"]["throttle"].get("pin") == 21)
dut.command("SET_THROTTLE_PCT", iParam=60); a, p = watch("OILPUMP_OUT", "us"); rec("SERVO output (LEDC servo)", a, "pulse=%dus @60%%" % p); dut.command("SET_THROTTLE_PCT", iParam=0)

# ── 3. INPUTS: freq + ADC + digital ────────────────────────────────
def cfg_in(hw):
    disable_all(hw)
    hw["sensors"]["n1_rpm"].update(enabled=True, pin=4, ppr=1.0)              # freq  <- S3 GPIO14
    hw["sensors"]["p1"].update(enabled=True, pin=32)                          # ADC1  <- S3 GPIO5
    hw["di_channels"][0].update(pin=27, active_h=True, role="none", label="DI", active_modes=0x1F)  # digital <- S3 GPIO2
dc.multi(cfg_in, check=lambda hw: hw["sensors"]["n1_rpm"]["enabled"] and hw["di_channels"][0]["pin"] == 27)
t.set("N1", round(45000/60.0, 1)); time.sleep(1.5); n1 = dut.data().get("n1"); t.set("N1", 0)
rec("FREQ input (N1 RPM / PCNT)", abs((n1 or 0) - 45000) < 3000, "drive 45000 -> %s" % n1)
t.set("IDLE_IN", 20000); time.sleep(0.7); hi = dut.data().get("p1_raw")
t.set("IDLE_IN", 0);     time.sleep(0.7); lo = dut.data().get("p1_raw")
rec("ADC input (GPIO32 range)", (hi or 0) - (lo or 0) > 2000, "high=%s low=%s" % (hi, lo))
t.set("FLAME", 1); time.sleep(0.4); don = (dut.data().get("di_channels") or [{}])[0].get("state")
t.set("FLAME", 0); time.sleep(0.4); doff = (dut.data().get("di_channels") or [{}])[0].get("state")
rec("DIGITAL input (DI channel)", don is True and doff is False, "on=%s off=%s" % (don, doff))

npass = sum(1 for _, ok in results if ok)
print("\n=== Classic ESP32 pin functions: %d/%d passed ===" % (npass, len(results)))
for n, ok in results:
    if not ok: print("  FAIL:", n)
t.close()
