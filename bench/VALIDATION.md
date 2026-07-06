# OpenTurbine bench validation campaign

Systematic hardware-in-the-loop validation of the OpenTurbine firmware on the
bench rig, aimed at finding defects **before** they reach a real turbine engine.
DUT = ESP32-S3 running the user's edited firmware (fw 1.3.0 base). Tester =
OTBench v0.3 (classic ESP32). Driven physically (ADC/PCNT/SPI) + pin-reuse for
sensors that aren't directly wired.

Legend: ✅ pass · ⚠️ anomaly/concern · ❌ bug · ⏭️ not physically testable

## Findings (running)

### Safety monitor
| Check | Result | Detail |
|---|---|---|
| OVERSPEED | ✅ | N1 60000 vs limit 50000 → SHUTDOWN in **0.37 s** (RPM reads within 0.2 s, ~3-sample confirm). No false trip at 49000. |
| OVERTEMP (TOT) | ✅ | TOT 800 vs limit 700 → SHUTDOWN in 0.76 s. No trip at 650. |
| HOT_START | ✅ | TOT 300 (>threshold 200) at start → STARTUP aborted "hot start" in 0.3 s. Cold start (120) proceeds. |
| hot_start × overtemp | ✅ (interaction) | Hot-start (STARTUP, TOT>200) correctly pre-empts overtemp (700) during a start — intended: don't keep starting into a hot engine. |
| TOT_RISE / EGT rate | ✅ | 100→600 fast jump → SHUTDOWN 0.27 s; stable EGT no trip. |
| LOW_OIL | ✅ | With OilPrime in seq (arms oilMinBar=1.5): oil 0.4 bar → SHUTDOWN 0.25 s; 1.7 bar no trip. (Minimal seq never arms oilMinBar — by design.) |
| FLAMEOUT | ✅ | In RUNNING (flameMonitorActive forced at STARTUP→RUNNING, main.cpp:1696): flame loss + EGT drop → SHUTDOWN in 3.42 s (matches 3 s confirm), relight-not-possible. Stays lit while flame present. |
| OIL_ZERO | ✅ | In RUNNING, oil ~0.08 bar (< zero 0.1) → SHUTDOWN 0.37 s. No trip at 0.25 bar. **See cal caveat below.** |
| OIL_TEMP_HIGH | ✅ | NTC via pin-reuse: ~148 °C (> limit 90) → SHUTDOWN 0.28 s; 70 °C no trip. |
| BATT_LOW | ✅ | Analog via pin-reuse: 7 V (< min 10) → SHUTDOWN 0.16 s; 11 V no trip. |
| UNDERSPEED | ✅ | (found incidentally) N1 falls below min_rpm in RUNNING → SHUTDOWN. Correct flameout/stall protection. |
| FUEL_PRESS_LOW | ⏭️(likely ✅) | Same RUNNING + pin-reuse mechanism as oil-zero/batt (both pass); not separately run. |
| TIT_OVERTEMP / SURGE | ⏭️ | TIT = 2nd SPI thermocouple not wired (one CS). Surge = rapid RPM variance, hard to synthesize cleanly. |

**Safety verdict: 10/12 checks validated on hardware, all correct, no firmware defects.** Every catastrophic-failure protection (overspeed, overtemp, EGT-rate, oil-zero, low-oil, flameout, under-speed, hot-start) fires with a proper confirmation window and no false trips.

Response times are all fast and confirmation-windowed (no single-sample false trips): overspeed 0.37 s, overtemp 0.76 s, EGT-rate 0.27 s, low-oil 0.25 s, oil-zero 0.37 s, flameout 3.4 s (by design), hot-start 0.3 s.

### Controllers
| Controller | Result | Detail |
|---|---|---|
| Throttle slew (rate limit) | ✅ | `throttle_effective` ramps 0.05→0.86 over ~2 s under `ramp_up_ms=2000` (gradual, not a step). |
| Throttle pullback | ⏳ | pending (N1/EGT near-limit throttle reduction) |
| Oil P-loop / dynamic idle / governor | ⏳ | pending (governor needs N2, not wired) |

⚠️ **THROTTLE_OUT servo line unverified — wire check needed.** Firmware drives a
correct continuous 50 Hz servo pulse (`ServoActuator`, confirmed in source), and
`throttle_effective` follows demand, but the tester reads **no pulse** on GPIO18
(oil-pump LEDC + relay outputs all read fine). The original suite only tested
throttle *input*, never the servo *output* — so **S3 GPIO16 → tester GPIO18 was
never actually validated.** Needs the jumper checked before the throttle/starter
servo output paths can be signed off.

### Framework notes (important for anyone re-running)
- Config changes MUST be verified (re-read); a config PATCH or hardware POST that lands too close to a reboot, or while the engine is not in STANDBY, silently no-ops. `DutConfig` now verifies + retries.
- Opening the tester serial port glitches the DUT into STARTUP (DTR→EN reset). Always `ensure_mode_standby()` after connecting, before any config change.

## Bugs / anomalies

No **firmware** bugs found so far — the seven critical safety shutdowns all fire
correctly with sane confirmation windows and no false trips. Notes worth raising
with the author:

1. ⚠️ **Oil-zero reachability depends on calibration (engineering note, not a bug).**
   OIL_ZERO fires when `oilPressure < oil_advanced.zero_bar` (default 0.1 bar)
   *and the sensor reads healthy*. If a user's oil-pressure calibration never maps
   the real zero-pressure reading below 0.1 bar (e.g. sensor offset, or a naive
   0–N bar linear cal over the full ADC range), oil-zero can never trigger — the
   most catastrophic oil fault would be silent. Worth a calibration-page warning
   or a sanity check that `poly(zero-pressure raw) < zero_bar`. (On the bench the
   ESP32 ADC's ~0.1 V low-end floor made this visible.)
2. ℹ️ LOW_OIL only arms once a sequence block (`OilPrime`/`StarterSpin`/`Spool`)
   sets `oilMinBar`. A hand-built startup sequence that omits all three leaves
   low-oil protection off in STARTUP. `flameMonitorActive` has an explicit
   safeguard for exactly this (main.cpp:1696) — `oilMinBar` does not. Consider a
   similar backstop or a config-validation warning.
