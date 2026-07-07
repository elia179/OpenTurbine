# dev/ — developer & validation material

Not needed to build or flash OpenTurbine — moved here to keep the repo root focused on the
firmware. To build the firmware you only need the root (`platformio.ini`, `src/`, `data/`,
`hardware_profile.h`, `partitions*.csv`) and PlatformIO.

- **bench/** — hardware-in-the-loop (HIL) validation rig: a second ESP32 "OTBench" tester,
  a Python harness, the campaign test scripts, and the validation records
  (`bench/VALIDATION.md`, `bench/ISSUES_FOUND.md`).
- **CODEMAP.md** — a map of the firmware source for contributors.
- **DESIGN_SPEC.md** — the design specification.
