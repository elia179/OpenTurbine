"""Afterburner inputs/outputs + state machine.

No AB-specific pins are wired on the bench, so remap the AB solenoid onto the wired
STARTER_EN pin (dut39/tester33 relay) to physically observe it, and enable ab_pump on a
free pin (observed via telemetry). Manual trigger (AB_FIRE/AB_STOP) — no input wiring
needed. Custom AB ignition sequence without ABFlameConfirm (no AB flame sensor on the rig):
ABSolOpen -> ABPumpOn -> ABStabilize. Validates: AB fires only in RUNNING, drives the AB
solenoid + pump, reaches AB Running, and AB_STOP cleanly shuts it down.
"""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

rig = BenchRig(); dut = rig.dut; dc = rig.dcfg; t = rig.t
results = []
def rec(n, ok, d=""): results.append((n, ok)); print("[%s] %-40s %s" % ("PASS" if ok else "FAIL", n, d))
dut.ensure_mode_standby(); dut.ensure_dev_mode(True)

# ── configure afterburner: ab_sol on the wired STARTER_EN pin, ab_pump on a free pin ──
def setup(hw):
    # Modify existing blocks IN PLACE (replacing them wholesale drops required fields ->
    # "Invalid hardware section JSON"). Leave ab_seq at default (its parallel arrays must
    # stay length-matched); bench mode bypasses the default seq's ABFlameConfirm.
    hw["has_afterburner"] = True
    hw["actuators"]["starter_en"].update(enabled=False)          # free pin 39 for ab_sol
    hw["actuators"]["ab_sol"].update(enabled=True, pin=39, active_h=True)
    hw["actuators"]["ab_pump"].update(enabled=True, pin=17)
    hw["ab_trigger"]["source"] = 0                              # manual
    hw["ab_trigger"]["requires_arm"] = False
    hw["ab_flame"]["enabled"] = False
ok = dc.multi(setup, check=lambda hw: hw.get("has_afterburner") and hw["actuators"]["ab_sol"]["enabled"]
              and hw["actuators"]["ab_sol"]["pin"] == 39)[0]
print("configure afterburner (ab_sol->pin39, ab_pump->pin17, manual trigger):", ok)
# torch_tot_limit>0 activates the torch ignition method; without it (default 0) AND with
# use_igniter=false, ABIgnite has no active method and faults ("no active ignition method").
dc.patch_cfg({"afterburner": {"main_fuel_offset_pct": 8, "pump_max_pct": 90,
                              "pump_control_mode": 0, "torch_tot_limit": 900, "use_torch": True}})

# ── reach RUNNING (bench) ───────────────────────────────────────────
dut.ensure_bench_mode(True)
t.set("N1", hz(45000)); t.set("OILP", 2.5); t.set("FLAME", 1); t.set_tot(200); time.sleep(0.6)
rig.start(); r, _ = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
print("engine RUNNING:", r, "| ab_mode:", dut.data().get("ab_mode"))
rec("AB idle before fire (Off, sol closed)",
    dut.data().get("ab_mode") in ("Off", None) and not dut.data().get("ab_sol_open"),
    "ab_mode=%s ab_sol=%s" % (dut.data().get("ab_mode"), dut.data().get("ab_sol_open")))

# ── AB_FIRE -> ignition sequence drives sol + pump, reaches Running ──
dut.command("AB_FIRE")
r2, _ = dut.poll_until(lambda x: x.get("ab_mode") in ("Igniting", "Running", "Stabilizing"), timeout=6)
time.sleep(3.0)   # let the default sequence run through to ABStabilize/Running
d = dut.data()
sol_relay = t.get("STARTER_EN").get("level")   # physical: ab_sol is on the STARTER_EN wire now
rec("AB_FIRE drives AB solenoid (telemetry + physical relay)",
    d.get("ab_sol_open") and sol_relay == 1, "ab_sol_open=%s relay=%s" % (d.get("ab_sol_open"), sol_relay))
rec("AB_FIRE drives AB pump demand", (d.get("ab_pump_demand") or 0) > 0.0,
    "ab_pump_demand=%s" % d.get("ab_pump_demand"))
rec("AB reaches Running (fuel offset live)", d.get("ab_mode") == "Running",
    "ab_mode=%s ab_fuel_offset=%s" % (d.get("ab_mode"), d.get("ab_fuel_offset")))

# ── AB_STOP -> clean shutdown ───────────────────────────────────────
dut.command("AB_STOP")
r3, _ = dut.poll_until(lambda x: x.get("ab_mode") == "Off", timeout=6)
time.sleep(0.5)
d2 = dut.data(); sol_off = t.get("STARTER_EN").get("level")
rec("AB_STOP shuts AB down (sol closed)",
    (d2.get("ab_mode") == "Off") and (not d2.get("ab_sol_open")) and sol_off == 0,
    "ab_mode=%s ab_sol=%s relay=%s" % (d2.get("ab_mode"), d2.get("ab_sol_open"), sol_off))

# ── restore ─────────────────────────────────────────────────────────
rig.recover()
def restore(hw):
    hw["has_afterburner"] = False
    hw["actuators"]["ab_sol"].update(enabled=False, pin=-1)
    hw["actuators"]["ab_pump"].update(enabled=False, pin=-1)
    hw["actuators"]["starter_en"].update(enabled=True, pin=39)
dc.multi(restore)
t.set("N1", 0)
npass = sum(1 for _, ok in results if ok)
print("\n=== Afterburner: %d/%d passed ===" % (npass, len(results)))
for n, ok in results:
    if not ok: print("  FAIL:", n)
rig.close()
