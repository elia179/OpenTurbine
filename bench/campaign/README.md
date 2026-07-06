# Validation campaign scripts

Hardware-in-the-loop test scripts run against the DUT during the validation
campaign (see `../VALIDATION.md` for results). Each opens the tester once,
reconfigures the DUT as needed (verified), drives stimulus, and asserts.

Run any of them from this folder (they add `../harness` to the path):

```
python safety_A.py     # overspeed, overtemp, hot-start
python safety_B.py     # EGT rate-of-rise, low-oil
python safety_C.py     # RUNNING-mode: oil-zero, flameout
python safety_D.py     # pin-reuse: oil-temp-high, batt-low
python ctrl_slew.py    # throttle-slew rate limiting
```

Prereqs: DUT (ESP32-S3) reachable at `http://192.168.4.1`, OTBench tester on a
COM port (edit the `Tester("COM3")` port if different), `pip install pyserial`.

Reusable helpers live in `../harness/otbench/` (`BenchRig`, `DutConfig`, `DUT`,
`Tester`). The one-off `*_demo.py` scripts (ntc/cal/tools/tot) reference absolute
scratchpad paths for data files and are kept as examples only.
