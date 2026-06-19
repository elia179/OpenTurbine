# Controllers subsystem audit

## Files reviewed
- /home/user/openturbine/src/engine/controllers/IController.h
- /home/user/openturbine/src/engine/controllers/ThrottleSlew.h
- /home/user/openturbine/src/engine/controllers/DynamicIdle.h
- /home/user/openturbine/src/engine/controllers/OilPressureLoop.h
- /home/user/openturbine/src/engine/controllers/PowerTurbineGovernor.h
- /home/user/openturbine/src/Hardware.h (initControllers around line 1143, runControllers around line 1152, applyConfig around line 396)
- /home/user/openturbine/src/main.cpp (limp-mode cap lines 1638-1645, enterRunning line 924, throttleDemand=0 line 927)
- /home/user/openturbine/src/system/Config.h and Config.cpp (defaults for gains/limits, fromJson)
- /home/user/openturbine/src/system/web/WebServer.cpp (PATCH /api/config handler, line 497-547)
- /home/user/openturbine/src/engine/EngineData.h (sensor health and demand fields)

## Findings (Critical -> Info)

### ECU-CTL-01: ThrottleSlew safety pullback gated on sensor health silently disarms overspeed/overtemp protection
- Severity: Critical
- Bug class: Fail-safe logic / state machine
- Location: /home/user/openturbine/src/engine/controllers/ThrottleSlew.h lines 50-58
- Description: The two safety pullback branches require `ed.n1Healthy` and `ed.totHealthy` respectively before any throttle reduction is applied. If a sensor drops to unhealthy mid-run (lead detached, ADC stuck, jump rejected by the sensor jump filter), the pullback never engages even though the engine may actually be at or beyond the soft limit. The last known good RPM/TOT in `ed`is also not consulted, and no replacement protection (such as freezing the demand or capping it) takes its place. The only remaining backstop is the safety monitor's hard shutdown, which is precisely what the soft pullback was supposed to avoid.
- Trigger: N1 thermocouple or hall sensor faults during high-power running. Sensor invalidates -> `n1Healthy=false` -> 95% pullback gate skipped -> throttle continues to climb if pilot/governor commands it.
- Impact: Direct path to overspeed or overtemp event with no soft intervention.
- Evidence: lines 50, 55: `if (ed.n1Healthy && ed.n1Rpm > rpmSoftLimit ...)` and `if (ed.totHealthy && ed.tot > totSoftLimit ...)`. No `else` branch reduces the demand when health is false.
- Suggested fix: When health flag is false, apply a precautionary cap (for example clamp `target` to the current `_current` so throttle cannot increase further) until the sensor recovers or the safety monitor takes action.
- Confidence: high

### ECU-CTL-02: DynamicIdle does not validate that N2 sensor is fitted before reading it for idle control
- Severity: Critical
- Bug class: Input validation / fail-safe
- Location: /home/user/openturbine/src/engine/controllers/DynamicIdle.h lines 45-48
- Description: When `Config::idleUseN2 == true` the controller selects `ed.n2Rpm` and `ed.n2Healthy`. EngineData defaults `n2Healthy = true` ("unfitted -> not a fault") at line 61 of EngineData.h. On a profile that enables `idleUseN2` but has no N2 sensor wired, `ed.n2Rpm` stays at 0 and `n2Healthy` stays `true`. The controller then computes `error = targetRpm - 0 = +44000`, which is well outside the deadband, so each tick adds positive ramp toward the ceiling of 1.0. `validateSequences` is mentioned as the intended check, but the controller itself does not protect.
- Trigger: `idleUseN2=true` configured on hardware without N2 sensor fitted, or N2 sensor present but in unfitted/default state.
- Impact: Idle floor ramps to 100% throttle while engine is in RUNNING. Severe overspeed / hot condition.
- Evidence: DynamicIdle.h:45-48 (no `hasN2Rpm` cross-check); EngineData.h:61 default `n2Healthy = true`.
- Suggested fix: Cross-check `HardwareConfig::hasN2Rpm` (or a dedicated `sensorFitted` flag) in `DynamicIdle::tick` and disengage if `idleUseN2` is selected but no N2 sensor is present. Also flip the EngineData default of `n2Healthy` to false until first valid read.
- Confidence: high

### ECU-CTL-03: OilPressureLoop failsafe latches to 60% duty even when oil is over-pressured at the moment of sensor loss
- Severity: Critical
- Bug class: Fail-safe direction
- Location: /home/user/openturbine/src/engine/controllers/OilPressureLoop.h lines 38-48
- Description: On `oilHealthy=false` for longer than `failsafeDelayMs`, the loop unconditionally writes `ed.oilPumpPct = failsafePct` (default 60%). The previously commanded duty (potentially much lower, e.g. 20%) is overridden upward. If the sensor failure happens because the pressure is unrealistically high (sensor saturated, line broken open in over-pressure regime) the controller raises pump duty further, worsening the condition. There is also no upward-rate-limit so the duty can step from min to 60% in a single tick.
- Trigger: Oil pressure sensor saturates or returns spurious low reading mid-run while pump is at minimum; sensor flagged unhealthy by the sensor module; after 1.5s the failsafe forces 60% duty.
- Impact: Oil pump overdrive into a failed pressure regulator. With centrifugal pumps this is benign; with positive-displacement pumps this can blow the oil filter or burst lines.
- Evidence: OilPressureLoop.h:44-46. No comparison against `_outputPct` before the assignment.
- Suggested fix: Use `failsafePct` only as a floor, never reduce or increase by more than a safe step per tick, and prefer to hold last good `_outputPct` if it was within a sensible band. Consider adding a separate "freeze last good" mode for short faults before falling to fixed duty.
- Confidence: medium

### ECU-CTL-04: OilPressureLoop failsafe latch never exits even after sensor recovers
- Severity: High
- Bug class: State machine / failsafe exit
- Location: /home/user/openturbine/src/engine/controllers/OilPressureLoop.h lines 38-51
- Description: Once the failsafe latch fires (sensor unhealthy for `failsafeDelayMs`), every subsequent tick where `oilHealthy` becomes true resets `_failsafeTimer = 0` and `oilFailsafeActive = false` AND immediately resumes the P-loop using `_outputPct` from before the fault. However, the `_outputPct` was never updated during the failsafe window (control flow returned early at line 47), and the duty actually being driven was `failsafePct`. So at the moment of recovery the loop snaps from 60% back to whatever the saved `_outputPct` was, causing a duty discontinuity that can dip pressure below `oilRunningMin` and false-trip the safety monitor. The reverse can also happen and cause a sudden surge.
- Trigger: Brief sensor dropout (loose connector, EMI) that exceeds `failsafeDelayMs` then recovers.
- Impact: Pressure transient + spurious oil shutdown, potential rapid pressure cycling.
- Evidence: lines 44-46 set `ed.oilPumpPct = failsafePct` but never update `_outputPct`. Lines 50-51 clear flags but skip seeding `_outputPct` from the new healthy reading.
- Suggested fix: On the healthy-recovery edge, seed `_outputPct = failsafePct` (or `ed.oilPumpPct`) so the P-loop continues smoothly. Optionally hold the failsafe latch for an additional dwell to avoid flapping.
- Confidence: high

### ECU-CTL-05: Concurrency race on Config::idleIGain / idleIMax / idleUseN2 between Core 0 (web POST) and Core 1 (DynamicIdle tick)
- Severity: High
- Bug class: Concurrency
- Location: /home/user/openturbine/src/engine/controllers/DynamicIdle.h lines 45, 85-86; /home/user/openturbine/src/system/web/WebServer.cpp lines 484-489, 517-538
- Description: `DynamicIdle::tick` reads `Config::idleUseN2`, `Config::idleIGain`, `Config::idleIMax` directly each call (they are not mirrored into the controller instance by `applyConfig`, see Hardware.h:489-496 which copies the other DI parameters but not these three). The `Config::*` statics are written from the AsyncWebServer task on Core 0 inside `Config::fromJson`, with no mutex. A POST during a controller tick can flip `idleUseN2` between the `useN2 =` read at line 45 and the `healthy =` read at line 47, producing an inconsistent (rpm-source, health-source) pair. A change to `idleIGain` from 0 to non-zero between ticks does not reset `_integrator`, so the next tick suddenly adds a stale (zero) integrator with a new gain. Half-torn floats on a 32-bit MCU are aligned-write atomic, but a `bool` flip combined with an `iGain` change is not.
- Trigger: Operator edits idle config via web while engine is running.
- Impact: One-tick inversion of selected RPM source (e.g. read n2Rpm but n1Healthy), or sudden integrator engagement. Both can drive throttleDemand wrong direction during the affected tick.
- Evidence: DynamicIdle.h:45-47 and :85-86 read Config statics; Hardware.h:489-496 copies only ramp/target/limit/multiplier into the instance; no synchronization in WebServer.cpp PATCH handler.
- Suggested fix: Mirror these three into the controller instance inside `applyConfig`, and either (a) defer `applyConfig` until STANDBY (already partly done for blocks at main.cpp:1326) for these critical knobs, or (b) take a short critical section around the read in tick().
- Confidence: high

### ECU-CTL-06: PowerTurbineGovernor pitch saturation handoff fights ThrottleSlew rate limit and is gain-asymmetric
- Severity: High
- Bug class: Logic / controller interaction
- Location: /home/user/openturbine/src/engine/controllers/PowerTurbineGovernor.h lines 101-123; /home/user/openturbine/src/Hardware.h lines 1200-1202
- Description: When pitch saturates at 1.0 (over-target N2, governor wants to add load) the controller skips pitch and instead applies `kp * error * dt` directly to `throttleDemand`. On the next tick `ThrottleSlew::tick` overwrites `throttleDemand` with its slewed value, discarding the governor's delta because ThrottleSlew reads `target = ed.throttleDemand` and rate-limits its own `_current` toward it. The governor's correction is therefore only "seen" by the slew controller as a step in `target`, not as an additive `_current` adjustment. With `rampDownMs=800ms` (default) and `kp=0.001` per RPM*s, a 1000 RPM overspeed produces `adj = 0.001 * 1000 * 0.005 = 0.005` per 5ms tick, so the target moves by 1.0 per second, but ThrottleSlew caps the actual `_current` at the slew rate. Net effect: governor cannot reduce throttle faster than `rampDownMs` allows, even during N2 overshoot transients. Compounded by DynamicIdle's floor running after the governor in the tick order (Hardware.h:1200-1202).
- Trigger: Power turboshaft load drop (rotor unload, generator shed) causing N2 overshoot while pitch is already coarse.
- Impact: N2 overshoot lingers; governor and slew oscillate against each other; ModifiedIdle floor or DI floor can pin throttle above where governor wants it.
- Evidence: PowerTurbineGovernor.h:120-123 writes `throttleDemand`; Hardware.h:1201-1202 runs DynamicIdle then ThrottleSlew after governor; ThrottleSlew.h:45 captures `target` from `ed.throttleDemand` each tick.
- Suggested fix: Have the governor coordinate with ThrottleSlew via a separate "trim" channel that bypasses the slew, or expose a `bumpCurrent()` API on ThrottleSlew. Document max governor authority versus slew rate. Add an explicit interlock so DynamicIdle's floor does not exceed governor demand when both are active.
- Confidence: medium

### ECU-CTL-07: Same-millisecond ticks in PowerTurbineGovernor and DynamicIdle produce dt=0 and zero correction
- Severity: High
- Bug class: Integer / float math on millis() based dt
- Location: /home/user/openturbine/src/engine/controllers/PowerTurbineGovernor.h lines 76-81; /home/user/openturbine/src/engine/controllers/DynamicIdle.h lines 53-55
- Description: With a 1-5 ms loop on ESP32 it is realistic for two consecutive ticks to land in the same `millis()` value (sub-ms loop on a short pass through the scheduler). When that happens, `dt = (now - _lastMs) / 1000.0f = 0`. In the governor, `pitchKp * error * dt = 0` and `kp * error * dt = 0`, so the tick produces zero correction (lost work). The governor has a guard `if (dt <= 0.0f || dt > 0.5f) dt = 0.01f;` that handles this, but DynamicIdle has no such guard for dt=0 -- `maxStep = dt / (rampMs / 1000.0f) = 0` so `rampStep = 0` and the integrator increment is also 0 for that tick. ThrottleSlew has the same dt=0 problem (line 42); the `rampMs > 0.0f ? dt / (rampMs/1000.0f) : 1.0f` guard handles the rampMs=0 case but not the dt=0 case. Net effect: spurious zero-correction ticks reduce effective bandwidth at fast loop rates.
- Trigger: Loop iteration faster than millis() granularity (likely during STANDBY/idle when there's little work).
- Impact: Reduced control bandwidth, integrator effectively integrates less than expected, P-corrections lost.
- Evidence: DynamicIdle.h:54 `float dt = (now - _lastMs) / 1000.0f;` no `dt<=0` guard; ThrottleSlew.h:42 same pattern.
- Suggested fix: Either clamp `dt` to a minimum (e.g. `if (dt <= 0) dt = 0.001f`), use `micros()` instead of `millis()` for sub-ms resolution, or accumulate skipped time until at least 1 ms has passed.
- Confidence: high

### ECU-CTL-08: ThrottleSlew loses its slew limit on the first tick after a long pause (large dt)
- Severity: High
- Bug class: Integer / float dt math, unbounded gap
- Location: /home/user/openturbine/src/engine/controllers/ThrottleSlew.h lines 41-71
- Description: `dt = (now - _lastMs) / 1000.0f` has no upper clamp. If the main loop is delayed (Wi-Fi event, deferred LittleFS save on Core 0 starving Core 1, debugger pause), `_lastMs` is stale and `dt` can be hundreds of milliseconds. Then `maxStep = dt / (rampUpMs/1000.0f)` can exceed 1.0 in a single tick, and `_current` jumps from min to max instantly with no slew at all -- defeating the entire purpose of this controller. DynamicIdle suffered the same defect and was fixed by the "always advance `_lastMs`" comment at line 50, but it still has no max-dt clamp.
- Trigger: Any event that stalls the loop for >= rampMs duration (Wi-Fi reconnect, config save, panic that gets recovered).
- Impact: Throttle steps to commanded value with no rate limit; can cause compressor surge, hot-section thermal shock.
- Evidence: ThrottleSlew.h:42-43 no clamp on `dt`; PowerTurbineGovernor.h:81 *does* clamp dt to 0.01 (so the pattern is known).
- Suggested fix: Clamp dt to a sensible max (e.g. 50 ms) in every controller. Apply the same clamp the governor already uses.
- Confidence: high

### ECU-CTL-09: ThrottleSlew::begin() snapshots throttleDemand AFTER enterRunning sets it to 0, so RUNNING entry slews from 0
- Severity: High
- Bug class: State machine handoff
- Location: /home/user/openturbine/src/main.cpp lines 924-940; /home/user/openturbine/src/engine/controllers/ThrottleSlew.h lines 31-37
- Description: `enterRunning()` at main.cpp:927 sets `ed.throttleDemand = 0` with the comment "clear ModifiedIdle/Spool demand; throttle controller takes over", then calls `Hardware::initControllers()` at line 940 which invokes `ThrottleSlew::begin()`. `begin()` reads `_current = constrain(ed.throttleDemand, 0.0f, 1.0f)` = 0. The very next loop tick will see DynamicIdle force `ed.throttleDemand` to the idle floor (e.g. 0.6), but ThrottleSlew now slews from 0 toward 0.6 over `rampUpMs` (default 600 ms). During that 600 ms the actuator output is below the idle floor. Worse, the `begin()` comment "Carry forward the current throttle demand so the physical actuator does not dip to zero when RUNNING is entered after the Spool block" describes the *intended* behaviour, but the caller has just zeroed the value so the carry-forward is a no-op.
- Trigger: Every RUNNING entry from STARTUP.
- Impact: Throttle dip / flame-out risk at handoff from Spool to RUNNING. The flame is most fragile precisely at this transition.
- Evidence: main.cpp:927 vs 940; ThrottleSlew.h:35 (carry-forward intent).
- Suggested fix: Either delete the `throttleDemand = 0` line at main.cpp:927 (let `ThrottleSlew::begin()` actually carry forward), or pass the desired idle floor to begin() explicitly, or have `enterRunning()` set `throttleDemand = max(spoolFinal, idleFloor)` before calling `initControllers`.
- Confidence: high

### ECU-CTL-10: Limp-mode throttle cap applied after ThrottleSlew leaves the slew controller's internal _current out of sync
- Severity: Medium
- Bug class: State machine / controller interaction
- Location: /home/user/openturbine/src/main.cpp lines 1638-1645
- Description: The limp cap clamps `ed.throttleDemand` after `Hardware::runControllers()` returns. ThrottleSlew has already stored `_current = ed.throttleDemand` (line 71). The cap reduces the published value but the controller's internal state retains the higher value. On the next tick, `target = constrain(ed.throttleDemand, ...)` reads back the *capped* value, but DynamicIdle / governor / pilot input may have rewritten `throttleDemand` above the cap before ThrottleSlew runs again, and ThrottleSlew will ramp UP from `_current` (the previous uncapped value) -- so the cap only takes one tick to enforce, but during that tick the actuator may briefly exceed the cap if `updateActuators` reads the slew controller's intermediate state instead of `ed.throttleDemand`. Also `Config::limpMaxThrottlePct` is not range-checked (could be configured negative or >100). And the cap does not engage in STARTUP, only RUNNING, so a fault that triggered limp mode just before RUNNING entry has no effect during startup ramps.
- Trigger: Limp mode active during RUNNING with controllers commanding above cap.
- Impact: One-tick over-cap blip on actuator; mis-configured cap silently bypassed.
- Evidence: main.cpp:1641-1643; no clamp on `limpMaxThrottlePct`.
- Suggested fix: Move the limp cap inside `ThrottleSlew::tick()` so `_current` itself is clamped, and validate `limpMaxThrottlePct` to [0,100] in Config::fromJson.
- Confidence: medium

### ECU-CTL-11: PowerTurbineGovernor unclamped Config gains (governorKp, pitchKp, pitchRampSec) can be set to dangerous values
- Severity: Medium
- Bug class: Input validation
- Location: /home/user/openturbine/src/engine/controllers/PowerTurbineGovernor.h lines 54-56, 106-108, 122; /home/user/openturbine/src/system/Config.cpp around line 526
- Description: `applyConfig` copies `Config::governorKp`, `Config::governorPitchKp`, `Config::governorPitchRampSec` into the controller with no range check. A user POST of `governor.kp = 10.0` produces `adj = 10 * 5000 * 0.005 = 250.0` per tick on a 5000 RPM error, which is then constrained to [0,1] but flips throttle to either end on the first tick. `governorPitchRampSec = 0` makes the comment "0 = unlimited" disable the slew cap (line 107: `pitchRampSec > 0.0f ? dt / pitchRampSec : 1.0f`), allowing pitch to step from 0 to 1 in a single tick (gearbox torque spike). Negative values are not rejected either (would invert sign of the correction).
- Trigger: Operator POSTs bad gain via web (intentional or fat-fingered).
- Impact: Bang-bang throttle, gearbox shock load, N2 oscillation.
- Evidence: PowerTurbineGovernor.h:107 only checks `> 0.0f`; Hardware.h:526-528 copies without clamping; Config.cpp does not appear to clamp these on load (search returned no clamp).
- Suggested fix: Clamp on load in Config::fromJson: `governorKp` to e.g. [0, 0.01], `pitchKp` similar, `pitchRampSec` to [1, 60]. Reject zero `pitchRampSec` outright.
- Confidence: high

### ECU-CTL-12: DynamicIdle integrator does not reset when idle is disengaged via misconfig (targetRpm <= 0 path returns early before reset)
- Severity: Medium
- Bug class: State machine / integrator reset
- Location: /home/user/openturbine/src/engine/controllers/DynamicIdle.h lines 41-43
- Description: When `targetRpm <= 0.0f || rpmLimit <= 0.0f` the tick returns immediately without clearing `_integrator` or `_idleFloor`. If the user temporarily zeroes `idleTargetRpm` to disable the idle loop and then re-enables it (without an engine restart -- e.g. via PATCH), the integrator retains its previous accumulation. The first re-engaged tick uses the stale integrator as a starting offset. Similar concern: the disengage path at line 58 (`!healthy || rpm > rpmLimit`) clears `_idleFloor` and `_integrator` correctly, but `_lastMs` was already advanced at line 55 -- good for dt freshness, but in combination with stale integrator on the misconfig path this is inconsistent.
- Trigger: Live edit of `idleTargetRpm` to 0 then back to a real value while running.
- Impact: First tick after re-enable applies a stale offset, briefly mis-floors throttle.
- Evidence: DynamicIdle.h:42-43 returns without touching `_integrator`.
- Suggested fix: On the early-return paths, also zero `_integrator` and `_idleFloor` and update `_lastMs`.
- Confidence: medium

### ECU-CTL-13: ThrottleSlew safety pullback compounds when both RPM and TOT exceed soft limits, can drive target below current to zero unintentionally fast
- Severity: Medium
- Bug class: Logic / interaction
- Location: /home/user/openturbine/src/engine/controllers/ThrottleSlew.h lines 47-58
- Description: Both pullback branches subtract from `target` independently: RPM pulls back up to 30% and TOT pulls back up to 20%, so a combined hot-and-fast condition can subtract 50% from the demand in a single tick. The `constrain(target - over * 0.30f, 0.0f, target)` upper bound is `target`, but the lower bound is 0, so over an RPM hard-limit excursion the demand snaps to 0 in one tick. With `rampDownMs = 800ms` this is *probably* slewed, but if dt is large (ECU-CTL-08) or rampDown is configured tight, the engine sees a fuel chop right at peak power -- which can cause flameout or surge as severe as the original overspeed.
- Trigger: Simultaneous transient excursion in both N1 and TOT.
- Impact: Aggressive double-pullback may itself induce flameout or surge instead of soft-recovering.
- Evidence: lines 52, 57: each subtracts independently; no coordination.
- Suggested fix: Cap combined pullback to one branch's maximum (e.g. `target = min(target, max(rpmTarget, totTarget))` style), or schedule pullback proportionally.
- Confidence: medium

### ECU-CTL-14: OilPressureLoop deadband + P-only loop cannot recover from saturated low-output when target rises (slow response)
- Severity: Low
- Bug class: Control quality
- Location: /home/user/openturbine/src/engine/controllers/OilPressureLoop.h lines 53-63
- Description: Pure P-controller with deadband: `_outputPct` only changes when `|error| > deadband`. When `oilTargetBar` rises (e.g. switching from idle target to throttle-mapped higher target as throttle increases, Hardware.h:1185-1188), the actual response depends entirely on `adjustScale` and is bounded by `[minPct, 100]`. With deadband=0.2 bar, the loop will sit at a steady-state offset of up to 0.2 bar below target indefinitely, even when adding a small integral term would fix it. The comment claims "Output saturation is handled by the constrain() call below; recovery from saturation is immediate once error reverses sign" but in a P-only loop with a sticky deadband, recovery is *not* immediate -- it requires error to cross deadband threshold.
- Trigger: Target step that lands within the deadband.
- Impact: Steady-state pressure offset on transitions; not safety-critical but undermines throttle-mapped oil pressure scheduling.
- Evidence: lines 59-62; no I-term.
- Suggested fix: Add a small I-term with windup limit, or shrink deadband to ~0.05 bar.
- Confidence: medium

### ECU-CTL-15: All controllers consume Config defaults at first tick if applyConfig was not called (zero-init case)
- Severity: Low
- Bug class: Initialization order
- Location: /home/user/openturbine/src/Hardware.h lines 396-530; /home/user/openturbine/src/main.cpp line 1538
- Description: `Hardware::applyConfig()` is called once during `setup()` at main.cpp:1538 after `Config::load()`. If `Config::load()` fails silently (e.g. corrupt LittleFS) and falls back to the in-class defaults shown in the header (ThrottleSlew rampUpMs=600, DynamicIdle targetRpm=44000, OilPressureLoop adjustScale=1.80, etc.), those defaults are reasonable. But if a future refactor moves applyConfig later or skips it on failure, the controllers' inline initialisers in the headers might or might not match Config defaults. There is no compile-time or run-time assertion linking the two sets of defaults. ECU-CTL-05 already notes idleIGain/idleIMax/idleUseN2 are not mirrored at all, so they are guaranteed to come straight from Config statics.
- Trigger: Future Config::load regression returning before assigning defaults; or new controller fields added without updating applyConfig.
- Impact: Latent bug: controller runs with header-default gains that may not match operator intent.
- Evidence: Two parallel default sets exist; only applyConfig links them, no runtime check.
- Suggested fix: Make controllers fetch their gains from Config:: directly (single source of truth) at begin(), and remove the inline header defaults, OR add a `static_assert` that all controller fields are mirrored.
- Confidence: low

### ECU-CTL-16: Asymmetric ramp in DynamicIdle treats falling RPM (need more throttle) as "ramp UP" -- direction convention is non-obvious and easy to mis-tune
- Severity: Low
- Bug class: Code clarity / hygiene
- Location: /home/user/openturbine/src/engine/controllers/DynamicIdle.h lines 70-76
- Description: `float rampMs = (error > 0) ? rampUpMs : rampDownMs;` -- `error = targetRpm - rpm`, so `error > 0` means RPM is below target and the controller needs to *raise* throttle (rampUp is correct). But `Config::idleRampUpMs = 10000`, `idleRampDownMs = 20000` (defaults), i.e. ramp DOWN is slower than ramp UP. The header comment says "asymmetric ramp (up slowly, down faster)" which is the *opposite* of the actual defaults. Either the comment is wrong or the convention is swapped between the controller code and the config defaults. A real misconfiguration here could cause idle hunting that takes 20s to converge or 10s to converge depending on which side of target you start from.
- Trigger: Tuner reads the comment, picks values matching the comment, gets the opposite asymmetry.
- Impact: Confused tuning, possibly slow idle recovery from a load drop.
- Evidence: DynamicIdle.h:17 vs Config.cpp:45-47 (`rampUpMs=10000, rampDownMs=20000`).
- Suggested fix: Reconcile comment with code+defaults; document direction convention next to the variable definitions.
- Confidence: medium

## Notes / unclear areas
- It is not 100% clear whether `Config::*` static float reads are guaranteed atomic on Xtensa LX6 (ESP32) at 4-byte alignment. Empirically yes for aligned 32-bit reads, but a bool plus float combined update (ECU-CTL-05) is not.
- The interaction between `Hardware::updateActuators()` and the limp cap (ECU-CTL-10) depends on whether `updateActuators` reads `ed.throttleDemand` (capped) or the slew controller's internal `_current` (not capped). Did not trace `updateActuators` for this audit; the limp finding assumes worst case.
- `enterRunning()` zeroing `throttleDemand` (ECU-CTL-09) may be deliberate to force the new controllers to re-establish demand from scratch. If so, the comment on `ThrottleSlew::begin()` about "carry forward" is misleading and should be rewritten.
- `Config::idleIGain` units are `idleIGain * dt * (error/targetRpm)` per tick; with default `idleIMax = 0.10` and typical idle drift of a few percent, the integrator math looks dimensionally consistent. Did not stress-test windup with very large errors.
- The PowerTurbineGovernor pitch handoff (ECU-CTL-06) interaction with ModifiedIdle floor was inferred from comments at Hardware.h:1193-1199 ("Tick order matters") and not verified by reading ModifiedIdle source.
