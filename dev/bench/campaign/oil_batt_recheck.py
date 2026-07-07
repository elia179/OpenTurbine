"""Re-prove OIL_ZERO and BATT_LOW with ONE reboot per test.

The batch/consolidated rechecks failed these two on reboot-race artifacts, not
firmware: back-to-back hardware POSTs (each a ~15 s reboot + WiFi reconnect) made
the verify read race the reconnect, so only_safety()/sensor reconfigs reported
'did not stick' and a leaked oil_temp-on-GPIO4 made the batt enable a (correct)
pin conflict. Here each test does a single combined hardware POST, settles, then
drives stimulus. N1 is always held so the always-on under-speed trip can't
contaminate the negative window.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

OILC = round(10.0 / 4095.0, 7)
N1_HOLD = hz(45000)
MINIMAL_SEQ = ["OilPumpOn", "TimedDelay", "IgniterOn", "FuelPumpIdle", "TimedDelay", "IgniterOff", "TimedDelay"]
MINIMAL_DLY = [0, 400, 0, 0, 400, 0, 400]

rig = BenchRig(); dc = rig.dcfg; dut = rig.dut; t = rig.t
print("settled:", dut.mode())

# ── OIL_ZERO: single POST = minimal seq + arm only oil_zero ──────────
print("\n-- OIL_ZERO (single-reboot, reachable threshold) --")
dut.ensure_mode_standby()
ok, _ = dc.multi(
    lambda hw: (hw.__setitem__("startup_seq", MINIMAL_SEQ),
                hw.__setitem__("startup_delay_ms", MINIMAL_DLY),
                [hw["safety"].__setitem__(k, (k == "oil_zero")) for k in hw["safety"]]),
    check=lambda hw: hw["startup_seq"] == MINIMAL_SEQ
                     and all(hw["safety"][k] is (k == "oil_zero") for k in hw["safety"]))
print("  arm oil_zero + minimal seq (verified):", ok)
print("  oil cal + zero_bar 0.5 (live):", dc.patch_cfg({
    "calibration": {"oil_poly": {"a": 0, "b": 0, "c": OILC, "d": 0, "x_min": 0, "x_max": 4095}},
    "oil_advanced": {"zero_bar": 0.5}})[0])
rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set_tot(120), t.set("OILP", 2.5), t.set("FLAME", 1))
rig.baseline(); rig.start()
r, d = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
print("  reached RUNNING: %s" % r)
neg = rig.stays_active(lambda: (t.set("N1", N1_HOLD), t.set("OILP", 1.0)), 3)   # ~2.4 bar > 0.5
t.set("OILP", 0.0); time.sleep(0.6)
d = dut.data(); print("  oil at DAC floor = %s bar (raw %s), armed=%s"
                      % (d.get("oil"), d.get("oil_raw"), dut.hardware()["safety"]["oil_zero"]))
pos = rig.detect_trip(lambda: (t.set("N1", N1_HOLD), t.set("OILP", 0.0)), "oil", 6)
rig.rec("OIL_ZERO", neg, pos); rig.recover()
dc.patch_cfg({"oil_advanced": {"zero_bar": 0.1}})

# ── BATT_LOW: single POST = batt on GPIO4 + arm only batt_low ────────
print("\n-- BATT_LOW (single-reboot, verified reconfig) --")
FS = 5.7 * 3.3
def batt_v(volts): return max(0.0, min(3.3, volts / FS * 3.3))
dut.ensure_mode_standby()
ok, _ = dc.multi(
    lambda hw: (hw["sensors"]["oil_temp"].update(enabled=False),
                hw["sensors"]["throttle_input"].update(enabled=False),
                hw["sensors"]["batt_voltage"].update(enabled=True, pin=4),
                [hw["safety"].__setitem__(k, (k == "batt_low")) for k in hw["safety"]]),
    check=lambda hw: hw["sensors"]["batt_voltage"]["enabled"]
                     and hw["sensors"]["batt_voltage"]["pin"] == 4
                     and not hw["sensors"]["oil_temp"]["enabled"]
                     and all(hw["safety"][k] is (k == "batt_low") for k in hw["safety"]))
print("  batt on GPIO4 + arm batt_low (verified):", ok)
if ok:
    print("  min 10V (live):", dc.patch_cfg({"safety": {"batt_volt_min_v": 10.0}})[0])
    rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("THROTTLE_IN", batt_v(14)), t.set("OILP", 2.5), t.set("FLAME", 1))
    rig.baseline(); time.sleep(1.5); rig.start()
    r, d = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
    print("  reached RUNNING: %s, batt reads %s V at 14V drive" % (r, dut.data().get("batt_voltage")))
    neg = rig.stays_active(lambda: (t.set("N1", N1_HOLD), t.set("THROTTLE_IN", batt_v(11))), 4)   # 11 > 10
    pos = rig.detect_trip(lambda: (t.set("N1", N1_HOLD), t.set("THROTTLE_IN", batt_v(7))), "volt")  # 7 < 10
    rig.rec("BATT_LOW", neg, pos); rig.recover()
else:
    print("  SKIP - batt reconfig still did not verify")

# ── restore the clean bench config (throttle_input on 4, batt/oil_temp off, disarm) ──
print("\n-- restore clean bench config --")
ok, _ = dc.multi(
    lambda hw: (hw["sensors"]["batt_voltage"].update(enabled=False),
                hw["sensors"]["oil_temp"].update(enabled=False),
                hw["sensors"]["throttle_input"].update(enabled=True, pin=4)),
    check=lambda hw: hw["sensors"]["throttle_input"]["enabled"]
                     and not hw["sensors"]["batt_voltage"]["enabled"]
                     and not hw["sensors"]["oil_temp"]["enabled"])
print("  throttle_input restored on GPIO4 (verified):", ok)

rig.summary("OIL_ZERO + BATT_LOW recheck (single-reboot)")
rig.close()
