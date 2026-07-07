# Continue here — HIL validation handoff

Pick-up point for continuing the OpenTurbine bench validation on another machine
(or a fresh session). Full results + findings are in `VALIDATION.md`.

## Physical setup
- **DUT** = ESP32-S3 running this branch's `src/` (edited OpenTurbine). Hosts its
  own WiFi AP (SSID `OpenTurbine`), web UI at `http://192.168.4.1`.
- **Tester** = classic ESP32 running OTBench **v0.3** (`bench/firmware/`), on a
  USB COM port (was `COM3` — change in scripts if different).
- Wiring = `bench/pinmap.json` + the MAX6675 thermocouple lines
  (S3 GPIO36/18/37 → tester 34/35/16).
- ⚠️ **Unverified wire:** THROTTLE_OUT (S3 GPIO16 → tester GPIO18). Firmware is
  fine (50 Hz servo confirmed) but the tester reads no pulse — seat/check that
  jumper before signing off the servo output paths.

## Reconnect the PC
- Wired Ethernet for internet + WiFi joined to SSID `OpenTurbine` for the DUT.
  (If pushing to GitHub stalls: the two default routes tie at metric 0 — raise
  the WLAN metric in an admin shell, or disconnect WiFi briefly to push.)

## Run tests
```
cd bench/harness && pip install -r requirements.txt        # pyserial
cd ../campaign && python safety_A.py                        # e.g. overspeed/overtemp/hot-start
```
Reusable helpers: `bench/harness/otbench/` — `BenchRig` (open tester + settle
DUT + start/detect-trip helpers), `DutConfig` (verified reconfig), `DUT`, `Tester`.
Flashing the tester: manual download mode (hold BOOT, tap EN, release BOOT) then
`pio run -e otbench -t upload` via PlatformIO's own 3.11 venv (see `README.md`).

## Done ✅
Framework · safety monitor (10/12 checks, all correct) · throttle-slew.

## Next ⏳ (task order)
1. Controllers: **throttle pullback** (drive N1/EGT near limit, confirm throttle
   demand is reduced *before* the hard shutdown), oil P-loop (drive oil low →
   oil_pct up), dynamic idle.
2. Sequencer: per-block actuator/timing checks, custom sequences, abort paths.
3. Config/persistence/validation (round-trips, reject illegal pins, reboot survival).
4. Rules engine, relight, remaining sensor-cal + actuator-output paths.

## Gotchas (learned the hard way — the framework already handles these)
- Opening the tester glitches the DUT into STARTUP (DTR→EN). Always
  `ensure_mode_standby()` **before** any config change. Config is locked unless STANDBY.
- **Verify every config write** (re-read); a POST/PATCH near a reboot silently
  no-ops. `DutConfig` retries until it sticks.
- Safety checks are **skipped in benchMode/skipSafetyChecks** and in STANDBY —
  test safety with a *real* start (safety runs in STARTUP).
- ESP32 ADC has a ~raw-84 low-end floor → min analog reading ≈ 0.2 bar / ~106 °C
  with a full-range cal. Pick thresholds/cals so the fault region is reachable.
- `oilMinBar` (low-oil arm) only set by OilPrime/StarterSpin/Spool blocks;
  `flameMonitorActive` is forced at STARTUP→RUNNING (main.cpp).
