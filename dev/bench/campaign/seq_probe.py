"""High-rate probe of a bench-mode startup: capture the igniter/fuel/oil pins on
the tester at ~50 ms while the DUT runs its startup sequence, plus the DUT's
current_block. Confirms the igniter physically fires (the basic suite's 0.2 s
poll aliases past the ~400 ms IgniterOn window)."""
import sys, time, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "harness"))
from otbench.benchrig import BenchRig

PORT = os.environ.get("OTBENCH_PORT", "COM3")

rig = BenchRig(PORT)
try:
    dut, tester = rig.dut, rig.t
    dut.ensure_mode_standby()
    dut.ensure_dev_mode(True)
    dut.ensure_bench_mode(True)
    tester.reset()
    code, resp = dut.start()
    print("start ->", code, resp.get("error") if code != 200 else "OK")
    if code != 200:
        sys.exit(1)
    ign_seen = fuel_seen = oil_seen = False
    blocks = []
    t0 = time.time()
    last = None
    while time.time() - t0 < 6.0:
        st = tester.state()
        ign = st.get("IGNITER", 0)
        fuel = st.get("FUEL_SOL", 0)
        oild = st.get("OILPUMP_OUT_duty", 0)
        d = dut.data()
        blk = d.get("current_block") or ""
        mode = d.get("mode")
        if blk and (not blocks or blocks[-1] != blk):
            blocks.append(blk)
        if ign == 1: ign_seen = True
        if fuel == 1: fuel_seen = True
        if oild > 0.02 or st.get("OILPUMP_OUT_level") == 1: oil_seen = True
        line = "t=%4.1fs mode=%-8s blk=%-12s IGN=%d FUEL=%d OILduty=%.2f ignFW=%s fuelFW=%s" % (
            time.time() - t0, mode, blk, ign, fuel, oild,
            d.get("igniter_on"), d.get("fuel_sol_open"))
        if line[7:] != (last[7:] if last else None):  # print on any change
            print(line)
        last = line
        time.sleep(0.04)
    dut.stop()
    dut.ensure_mode_standby()
    dut.ensure_bench_mode(False)
    dut.ensure_dev_mode(False)
    print("\nblocks seen:", " -> ".join(blocks))
    print("igniter fired:", ign_seen, "| fuel opened:", fuel_seen, "| oil pump ran:", oil_seen)
finally:
    rig.close()
