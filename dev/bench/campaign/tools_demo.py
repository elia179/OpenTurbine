import sys, time
sys.path.insert(0, r"C:\Users\elial\Documents\Dev\OpenTurbine\bench\harness")
from otbench import DUT, Tester

dut = DUT()
dut.ensure_mode_standby()
t = Tester("COM3").open()

# (tool command, tester signal, telemetry key, measure kind)
tools = [
    ("FUEL_PRIME",    "THROTTLE_OUT", "throttle_effective", "pwm"),
    ("OIL_PRIME",     "OILPUMP_OUT",  "oil_pct",            "pwm"),
    ("IGN_TEST",      "IGNITER",      "igniter_on",         "level"),
    ("FUEL_SOL_TEST", "FUEL_SOL",     "fuel_sol_open",      "level"),
    ("START_TEST",    "STARTER_OUT",  "starter_demand",     "pwm"),
]

print("%-15s %-13s %-22s %s" % ("tool", "actuator pin", "DUT telemetry", "tester measurement"))
print("-" * 78)
for cmd, sig, key, kind in tools:
    dut.ensure_mode_standby()
    code, resp = dut.command(cmd)
    deadline = time.time() + 14
    while code == 409 and "already active" in str(resp.get("error", "")) and time.time() < deadline:
        time.sleep(0.5); code, resp = dut.command(cmd)
    if code != 200:
        print("%-15s %-13s SKIP: %s" % (cmd, sig, resp.get("error")))
        continue
    pin, tel, meas = False, None, ""
    end = time.time() + 2.0
    while time.time() < end:
        if kind == "pwm":
            us, hz, duty, lvl = t.get_pwm(sig)
            if us > 500 or duty > 0.02 or lvl == 1:
                pin = True; meas = "%d us, %.2f duty" % (us, duty)
        else:
            if t.get_level(sig) == 1:
                pin = True; meas = "level HIGH"
        v = dut.data().get(key)
        if v:
            tel = v
        if pin and tel is not None:
            break
    print("%-15s %-13s %-22s %s" % (cmd, sig, "%s=%s" % (key, tel), meas if pin else "(idle)"))
    dut.poll_until(lambda x: not x.get(key), timeout=12)

t.close()
dut.ensure_mode_standby()
