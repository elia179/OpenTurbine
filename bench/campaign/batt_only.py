"""BATT_LOW on hardware, with a robust reboot wait.

edit_hw()'s _wait_reboot returns as soon as /api/status answers, but a hardware
POST answers status *before* the reboot actually drops (documented gotcha), so a
verify read can catch the pre-reboot config and wrongly report 'did not stick'.
Here we POST once, wait for the DUT to actually go DOWN, then come back, settle,
and only then verify — removing the race so the batt firmware logic can be tested.
"""
import sys, time, os, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.benchrig import BenchRig, hz

N1_HOLD = hz(45000)
FS = 5.7 * 3.3
def batt_v(volts): return max(0.0, min(3.3, volts / FS * 3.3))

rig = BenchRig(); dut = rig.dut; t = rig.t

def hard_reboot_post(mutate):
    hw = dut.hardware(); mutate(hw)
    code, resp = dut._post("/api/hardware", hw)
    print("  POST -> %s %s" % (code, json.dumps(resp)[:80]))
    if code != 200:
        return False
    # wait for it to actually drop
    dropped = False
    for _ in range(30):
        try:
            dut.status(); time.sleep(0.4)
        except Exception:
            dropped = True; break
    # wait for it to come back
    for _ in range(60):
        try:
            dut.status(); time.sleep(2.0)
            print("  reboot complete (dropped=%s)" % dropped)
            return True
        except Exception:
            time.sleep(1)
    return False

print("settled:", dut.mode())
dut.ensure_mode_standby()
print("\n-- BATT_LOW --")
hard_reboot_post(lambda hw: (
    hw["sensors"]["oil_temp"].update(enabled=False),
    hw["sensors"]["throttle_input"].update(enabled=False),
    hw["sensors"]["batt_voltage"].update(enabled=True, pin=4),
    [hw["safety"].__setitem__(k, (k == "batt_low")) for k in hw["safety"]]))
h = dut.hardware()
print("  verify: batt.en=%s pin=%s  batt_low armed=%s  throttle.en=%s"
      % (h["sensors"]["batt_voltage"]["enabled"], h["sensors"]["batt_voltage"]["pin"],
         h["safety"]["batt_low"], h["sensors"]["throttle_input"]["enabled"]))
ok = h["sensors"]["batt_voltage"]["enabled"] and h["safety"]["batt_low"]
if ok:
    print("  min 10V (live):", rig.dcfg.patch_cfg({"safety": {"batt_volt_min_v": 10.0}})[0])
    rig.baseline = lambda: (t.set("N1", N1_HOLD), t.set("THROTTLE_IN", batt_v(14)), t.set("OILP", 2.5), t.set("FLAME", 1))
    rig.baseline(); time.sleep(1.5); rig.start()
    r, d = dut.poll_until(lambda x: x.get("mode") == "RUNNING", timeout=20)
    print("  reached RUNNING=%s, batt=%s V at 14V drive" % (r, dut.data().get("batt_voltage")))
    neg = rig.stays_active(lambda: (t.set("N1", N1_HOLD), t.set("THROTTLE_IN", batt_v(11))), 4)
    pos = rig.detect_trip(lambda: (t.set("N1", N1_HOLD), t.set("THROTTLE_IN", batt_v(7))), "volt")
    rig.rec("BATT_LOW", neg, pos); rig.recover()
else:
    print("  batt reconfig failed even with robust wait -> real config issue")

# restore clean bench config
print("\n-- restore throttle_input on GPIO4 --")
hard_reboot_post(lambda hw: (
    hw["sensors"]["batt_voltage"].update(enabled=False),
    hw["sensors"]["oil_temp"].update(enabled=False),
    hw["sensors"]["throttle_input"].update(enabled=True, pin=4)))
h = dut.hardware()
print("  throttle.en=%s batt.en=%s oil_temp.en=%s"
      % (h["sensors"]["throttle_input"]["enabled"], h["sensors"]["batt_voltage"]["enabled"], h["sensors"]["oil_temp"]["enabled"]))
rig.summary("BATT_LOW recheck")
rig.close()
