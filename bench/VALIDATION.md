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

---

## Session 2 — 2026-07-07 (fw 1.4.0, S3 + tester)

Full regression re-run plus guides/UI/feature work. Real-problem log:
`bench/ISSUES_FOUND.md`. Known open item: DUT went unreachable after a flash near
the end (flash succeeded + hash-verified; AP didn't return, PC couldn't re-associate)
— items marked ⏳ are implemented/ready but await a DUT power-cycle to validate.

### Validated GREEN on hardware this session
- ✅ **Safety monitor** — TOT_RISE trips 0.48 s; OIL_ZERO trips 0.08 s once properly
  armed (the batch "FAIL" was the `only_safety` reboot-race verify artifact, not a
  defect); safety-block config round-trips correctly.
- ✅ **ServoActuator on S3** (session-1 open item RESOLVED) — attach-retry fix lands;
  throttle ESC ramps smoothly 0→full in ~2 s (`ctrl_slew`).
- ✅ **Throttle slew + N1/EGT pullback**, incl. under the governor, respecting the fuel floor.
- ✅ **Governor, both flavours** — throttle-driven (winds fuel to hold N2, respects the
  fuel floor at 0.08) and prop-pitch/load-driven (winds pitch, leaves throttle to pilot).
- ✅ **Fuel-pump minimum-spin floor** — 15% floors at 0.150, 10% at 0.100, 0 = no floor
  (throttle → ~0). Replaces the old fixed 8% idle floor by design.
- ✅ **Relight** 5/5, **rules** 6/6 (thresholds + hysteresis + shutdown), **sequencer** (blocks,
  gates, aborts, ImmediateCut), **config** persistence/reboot/illegal-pin/out-of-range-reject.
- ✅ **Oil P-loop** drives the pump to target pressure (sign response).

### Firmware fix this session
- ❌→✅ **OilPrime silent no-prime when the oil control loop is disabled.** With an oil
  sensor, `OilPrime` sets a pressure target and relies on `g_ctrlOilLoop` to drive the
  pump; if the loop is off, nothing drives it → times out into an abort with no reason.
  Added a preflight `seq_issue` warning (main.cpp sequence validation, OilPrime case).
  Confirmed: with the loop enabled OilPrime drives the pump to 100%.

### Engineering notes from session 1 — now addressed
- Note 1 (oil-zero reachability): the Calibration page now warns after an oil fit if the
  curve never reads below the zero-pressure threshold at the low end (OIL_ZERO could be silent).

### Validated GREEN after the DUT was power-cycled (firmware + UI reflashed)
- ✅ **New preflight warnings** both fire on hardware: OilPrime-needs-oil-loop, and
  low-oil-arming (sequence with no oil-arming block).
- ✅ **Standby-oil SET-PRESSURE mode** 5/5 (`standby_oil_pressure_test.py`): regulates to
  the target bar, floors at feed_pct, disengages + releases the pump when windmilling stops,
  and fixed-% mode (feed_bar=0) is unchanged.
- ✅ **Calibration pipeline** 4/4 (`calibration_pipeline_test.py`): oil cubic scaling, flame
  threshold tracking, fuel-pump-min save, oil zero-reachability.
- ✅ **Digital inputs** 3/3 (`di_switch_test.py`): DI channel debounce/state (pin-reused),
  START switch initiates start, STOP switch shuts down.
- ✅ **Rules engine** reconfirmed 6/6 + extended (LT operator + OIL_PRESS source + IGNITER
  actuator) on the new firmware.
- ✅ **Afterburner** 5/5 (`afterburner_test.py`): AB_FIRE opens the AB solenoid (telemetry +
  physical relay on the remapped pin), drives the AB pump to 90%, reaches AB Running with the
  main-fuel offset live, and AB_STOP shuts it down cleanly. New engineering note filed:
  default AB ignition faults ("no active ignition method") when torch has no EGT cap and the
  AB igniter is off — see ISSUES_FOUND.md #7.
- ✅ **Guides/UI** deployed via uploadfs; new firmware confirmed live (`governor_mode`,
  `standby_oil_feed_bar` telemetry present).

**All planned HIL validation is complete.**

---

## v1.5.0 sign-off — verified on BOTH chips (2026-07-07)

Both open follow-ups resolved: OilPrime now self-drives the pump when the oil loop is off
(verified: oil_pct 80, duty 0.81 with the loop disabled), and afterburner ignition falls back
to the fitted igniter (verified: default AB reaches Running). Both new-problem fixes done too:
the tester now drives N1/N2 **independently** (raw ESP-IDF LEDC per-timer), which unblocked a
**direct** N1-max pullback-under-governor test (N1 65k → throttle 1.0 → 0.08); and the "GPIO17
servo quirk" was root-caused as a dead bench jumper (DUT17↔tester19), not firmware.

**Cross-platform (role-reversed rig: OpenTurbine on the classic ESP32, S3 as tester):** every
reachable pin FUNCTION validated on the classic ESP32 — **8/8**: LEDC PWM out, servo out
(1599 µs, full 16-bit), 3× digital out, freq/RPM in (PCNT), ADC in (0↔4095), digital in. Plus
a clean serial boot (LittleFS, sequencer validate, WiFi, web server). The ServoActuator fix is
platform-correct: **16-bit on the classic, 12-bit fallback only on the S3.** OpenTurbine builds
for both `esp32dev` and `esp32s3dev`.

Not provable on this bench (wiring limits, NOT firmware): classic thermocouple SPI (TOT jumpers
land on input-only pins), a full analog sweep (S3 tester has no DAC), MAVLink (no UART jumper),
and a real engine start. Classic-ESP32 rule to document for testers: analog sensors must use
**ADC1** pins so WiFi doesn't disturb them.

Normal bench restored (S3 = OpenTurbine v1.5.0 DUT, classic = OTBench tester); smoke test green.
