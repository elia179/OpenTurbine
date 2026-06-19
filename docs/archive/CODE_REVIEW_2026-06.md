# OpenTurbine — Independent Code Review

> **Archived audit snapshot:** this file records a review of commit `93ec5a2`
> and firmware 1.1.0 from 2026-06-10. It is kept for traceability, not as the
> current beta status or user guidance. Use `README.md`,
> `docs/BETA_USER_GUIDE.md`, `docs/internal/BETA_READINESS_PLAN.md`, and `CHANGELOG.md`
> for current release-facing information.

**Date:** 2026-06-10 · **Commit reviewed:** `93ec5a2` (main) · **Firmware version:** 1.1.0
**Scope:** full firmware source (`src/`, `hardware_profile.h`, build config), web UI (`data_src/`), docs, prior `docs/archive/audit-2026-06/` findings, build verification.

---

## 1. Executive summary

**This is unusually good code for a hobby-class engine controller.** The architecture is sound, the safety thinking is visible everywhere (comments explain *why*, not *what*), and the remediation of the previous static audit was real — of 137 prior findings, **76 are fixed, 18 partially fixed, 40 still present** (mostly low-severity hygiene). The firmware **builds clean: zero compiler warnings**, RAM at 26.6%.

The three things I'd treat as blockers before anyone runs fuel through this with the current code:

1. **Unauthenticated control plane on an open Wi-Fi AP** (prior finding ECU-WEB-01/09, still present). Anyone in RF range can start the engine, flash firmware, or factory-reset. The AP password is optional and defaults to open.
2. **A custom AB ignition sequence without `ABStabilize` latches afterburner fuel on** (new finding, §4 F1).
3. **The calibration page is not mode-aware** — it can silently produce a bogus oil/flame calibration if the engine isn't in STANDBY, and it fires the igniter with no confirmation (new finding, §4 F3).

Also notable: **flash is at 90.6% of the 1.5 MB OTA slot.** A few more features and OTA stops fitting. Start planning now (drop `-O` level experiments, trim ArduinoJson usage, or move to an 8 MB-flash module for new builds).

---

## 2. What's genuinely good (credit where due)

- **Cross-core architecture is disciplined.** All control on Core 1, all flash/network I/O on Core 0, commands one-way via a FreeRTOS queue, telemetry via 32-bit-atomic `volatile` reads. The reasoning is *documented in the code* ([EngineData.h:15-33](src/engine/EngineData.h)) including its accepted weaknesses (torn string reads are display-only).
- **Config persistence is better than most commercial firmware:** temp-file + backup + rename, refuses to overwrite an unreadable file, mutex-serialized writers, deferred saves so Core 1 never touches flash ([Config.cpp:789-842](src/system/Config.cpp)).
- **The sequence validator** ([main.cpp:152-434](src/main.cpp)) cross-checks every configured block against fitted hardware and blocks START on errors — e.g. it catches `idleUseN2` without an N2 sensor, which would otherwise be a throttle runaway. This is the kind of defense-in-depth most hobby ECUs don't have.
- **Controllers are properly engineered:** dt-scaled gains (loop-rate independent), dt clamps, division-by-zero guards, sensor-loss interlocks (ThrottleSlew freezes target when N1/TOT unhealthy), anti-windup on the idle integrator, prop-pitch-primary governor with slew caps.
- **Wraparound-safe timer comparisons** (`deadlineExpired()` uses signed subtraction), seeded DI debounce state to suppress boot-time false edges, `fuelEverOpened` distinguishing safe-abort from hot-abort paths.
- **Bench/dev mode gating is layered correctly in firmware:** safety-check bypass requires dev mode + bench mode + STANDBY, dev mode toggle requires STANDBY.
- **The OTA handler closes its own race** (reserves `_otaInProgress` *before* checking outputs, re-checks STANDBY per chunk).
- The previous audit response (commit `aba6339`) fixed things *systematically* — value-clamping pass over all config fields, JSON pre-validation, atomic saves, request-ownership on shared web buffers — not just point patches.

---

## 3. Verification performed

| Check | Result |
|---|---|
| `pio run -e esp32dev` | **SUCCESS**, 0 warnings. RAM 26.6%, **Flash 90.6%** of OTA slot |
| `esp32s3dev` build | Not run (separate toolchain download; recommend CI for both) |
| `data/*.gz` vs `data_src/` | 7 HTML pages byte-identical; **app.js and style.css differ** (CRLF/LF only — see F5) |
| Docs vs code | Archived 1.1.0-era assessment. Current README, CODEMAP, DESIGN_SPEC, beta docs, and changelog have been updated since this review. |
| Prior `docs/archive/audit-2026-06/` findings | All 137 verified individually — see §5 |

Note: your local PlatformIO venv had a corrupted `charset-normalizer` package that broke `pio` entirely mid-review; I repaired it (removed the broken package remnants, reinstalled). If `pio` misbehaves again, `pip install --force-reinstall platformio` inside `%USERPROFILE%\.platformio\penv` is the fix.

---

## 4. New findings (not in the previous audit)

Ordered by priority.

### F1 — AB fuel latches on if a custom AB sequence lacks `ABStabilize` — **High (safety)**
`abMode` only transitions `Igniting → Running` in `ABStabilize::onExit()` ([ABStabilize.h:48-53](src/engine/sequencer/blocks/ABStabilize.h)). If the user's AB ignition sequence ends without it (it's fully user-editable), `abSequenceDone()` ([main.cpp:855-867](src/main.cpp)) leaves `abMode == Igniting`. In that state `checkABTrigger()` ignores trigger release (`case Igniting: break`), so **AB solenoid + pump stay on indefinitely** — the only way out is engine STOP. `validateSequences()` does not require a terminal `ABStabilize`.
**Fix:** in `abSequenceDone()`, if `!_abInShutSeq && abMode == Igniting`, either set `abMode = Running` or shut AB down; and add a validator warning for AB sequences not ending in `ABStabilize`.

### F2 — LEDC actuator floods serial at 0%/100% duty — **Medium-High (loop timing)**
[LEDCActuator.h:49](src/hal/actuators/LEDCActuator.h): the log condition `duty == _maxDuty || _lastDuty == _maxDuty` is **true on every call** while the output sits at full duty. `updateActuators()` calls `set()` every loop iteration and the loop is uncapped, so during oil prime (100% pump) or full LEDC throttle the loop prints continuously; `Serial.printf` blocks when the TX buffer fills, adding multi-ms jitter to the control loop exactly when the engine is at full power.
**Fix:** log on edges only: `if ((duty == 0) != (_lastDuty == 0) || (duty == _maxDuty) != (_lastDuty == _maxDuty))`.

### F3 — Calibration page is not mode-aware — **High (safety/UX)**
`calibration.html` never checks engine mode. Its oil wizard sends `SET_OIL_PCT` and the flame wizard sends `IGN_TEST` — both ignored by firmware outside STANDBY ([main.cpp:1409-1435](src/main.cpp)) — yet the wizard proceeds to capture data against a pump that never moved, **silently producing a wrong calibration**. The flame wizard also fires the igniter with no confirmation.
**Fix:** gate both wizards on `mode === 'STANDBY'` and confirm before `IGN_TEST`.

### F4 — Low oil renders as an *empty* gauge, not a red one — **Medium (UX, safety-adjacent)**
[app.js:519-533](data_src/app.js): in RUNNING, `pctForColor = oil < oil_running_min ? 0 : 80`, and the bar only turns red at ≥95%. So the single most fault-critical parameter displays as an empty neutral bar when dangerously low. The text warning fires, but the primary visual never goes red.

### F5 — Shipped web assets not reproducible from source — **Low-Medium**
`data/app.js.gz` and `data/style.css.gz` decompress to content that differs from `data_src/` by CRLF↔LF only. Functionally equivalent today, but the build artifacts aren't reproducible and nothing would catch real drift.
**Fix:** re-run `tools/gzip_data.py`, commit, add `.gitattributes` pinning line endings, and add a CI check `gunzip(data/x.gz) == data_src/x`.

### F6 — `FuelPumpRamp` discards its result on normal completion — **Medium (verify intent)**
[AdvancedBlocks.h:133-138](src/engine/sequencer/blocks/AdvancedBlocks.h): `onExit()` zeroes `fuelPump2Demand` on *every* exit path, including `Complete` — so ramping to `endPct` is undone one tick later. The comment only justifies the abort/fault case. If the intended pattern is "ramp, then a later `FuelPump2Set` holds it," document that; otherwise only zero on abort/fault.

### F7 — START during Extra Cooldown silently kills oil prime on sensor-less builds — **Low-Medium**
`handleCommand(START)` doesn't check `extraCooldownActive`. Sequence: START → `OilPrime::onEnter()` sets `oilPumpPct = startupOilPct` (no-oil-sensor path) → later same tick `checkExtraCooldown()` sees mode ≠ STANDBY and zeroes `oilPumpPct`/`starterDemand` ([main.cpp:575-581](src/main.cpp)). `onEnter` never re-runs, so the prime runs its full timeout **with the pump at 0%** and then *completes normally* — engine starts unprimed. (Sensor-fitted builds are fine; the P-loop re-drives the pump.)
**Fix:** block START while `extraCooldownActive` (it's already in `anyToolTimerActive()` — just check it in the START handler), or cancel cooldown *before* starting the sequencer.

### F8 — `enterStandby()` clears only 11 of 16 tool timers — **Low**
[main.cpp:1199-1209](src/main.cpp) clears the newer tool timers but not `_fuelPrimeUntilMs`, `_oilPrimeUntilMs`, `_ignTestUntilMs`, `_startTestUntilMs`, `_idleTestUntilMs`. Consequences are benign (stale timers expire to "off" states) but `anyToolTimerActive()` returns true briefly after a run, blocking new tool commands for no reason. Clear all of them.

### F9 — START/STOP hardware switches are undebounced — **Low-Medium (hardening)**
DI channels get configurable debounce; the dedicated start/stop pins are raw `digitalRead` edges ([main.cpp:1639-1694](src/main.cpp)). STOP noise fails safe; **START noise starts the engine.** A floating/long-leaded start wire with `startPullup=false` is one EMI burst from a start command. Require N ms of stable assertion.

### F10 — One 100 ms RPM glitch permanently latches limp mode — **Behavioral note**
A single-sample `JUMP` fault makes `n1Healthy` false for one tick; in RUNNING, [SafetyMonitor.h:271-273](src/engine/SafetyMonitor.h) latches `limpMode = true` with no auto-clear. Deliberate conservatism, but with `rpmJumpThreshold` at default a noisy hall sensor will limp every flight. Consider requiring the fault to persist for ≥2 samples before latching.

### F11 — Hardware/Sequence pages hold two WebSockets vs firmware's 1-client cap — **Medium (web)**
`app.js` auto-connects on every page; `hardware.html` and `sequence.html` open a second raw WS. Firmware enforces `cleanupClients(1)`, so the page's two sockets evict each other and live mode updates stall intermittently — weakening the "save only in STANDBY" UI guard that depends on a live `mode` value.

### F12 — Other web findings (from the deep UI review)
- Dashboard START confirm does **not** mention bench mode being active (in which *all* safety shutdowns are bypassed). Make START-while-bench a distinct hard confirm.
- Config page's Dev Mode button has no confirmation; Tools page gates the same toggle behind a confirm + STANDBY check. Make them consistent.
- `confirmStart()`'s empty-sequence guard **fails open** on a fetch error ([index.html:456](data_src/index.html)).
- STOP failure feedback is a dismissable `alert()` — needs a persistent "STOP NOT CONFIRMED" state.
- `openCustomBlockDialog` in sequence.html is dead code — no button ever calls it.
- Sequence page idle preview divides by 4095 instead of using the saved idle calibration ([sequence.html:1532](data_src/sequence.html)).
- Tooltip escaping in tools/sequence pages escapes only `"` — latent XSS footgun if descriptions ever include config-derived text. (Stored-config → dashboard paths are properly `textContent`-safe.)

### F13 — Minor / hygiene
- `faultDescription[192]` truncates several of the longer fault explanations mid-sentence.
- The chained-`snprintf` pattern (`n += snprintf(buf+n, sizeof(buf)-n, …)`) in FlightRecorder/SessionLogger has no overflow guard; if `n` ever exceeded the buffer, `sizeof(buf)-n` underflows (size_t). Safe with current sizes — add a clamp anyway.
- ClusterSerial TX frames are CRC'd, but RX commands are plain text lines (`START\n` etc.) with no checksum — asymmetric integrity on a wire that can command START.
- On ESP32-S3 (8 LEDC channels vs 16), a heavily-optioned build can exhaust LEDC channels; the only diagnostic is a serial "FAILED" line. Count channels in `validateJson`.
- Duplicate peak tracking: oilTemp/battVoltage maxima are updated both in `updateSensors()` and the main loop.
- `webTask` has no TWDT subscription (prior finding RTOS-11) — a wedged web task silently kills telemetry/logging while the engine runs.

---

## 5. Status of the previous audit (137 findings)

Full verification was done finding-by-finding. **Tally: 76 fixed, 18 partially fixed, 40 still present, 3 not-an-issue.** Of 16 Criticals: 11 fixed, 3 partial, 2 still present. The remediation commit (`aba6339`) was systematic, not cosmetic. The CHANGELOG does not credit any of this work — worth adding an entry.

**Still-present items that matter most** (all verified against current source):

| ID | What | Where |
|---|---|---|
| ECU-WEB-01 + WEB-09 | **No auth on any endpoint; AP open by default.** RF range = engine start + OTA flash + factory reset | [WebServer.cpp:304-325, 943-962](src/system/web/WebServer.cpp) |
| ECU-CTL-03 | Oil failsafe unconditionally forces `failsafePct` (60%) — can *lower* duty if the loop was driving higher | [OilPressureLoop.h:38-47](src/engine/controllers/OilPressureLoop.h) |
| ECU-SEN-01 | Flame sensor `isHealthy()` hardwired `true` — railed sensor silently fakes/misses flameout when source=flame | [AnalogSensor.h:143](src/hal/sensors/AnalogSensor.h) |
| ECU-LIM-14 | Standby windmill oil feed doesn't check `n1Healthy` — zero-stuck N1 skips bearing protection | [main.cpp:675](src/main.cpp) |
| ECU-ACT-04 | Relay-type oil pump goes full-on at any demand > 0% | [Hardware.h:1041-1043](src/Hardware.h) |
| ECU-CAL-07 | Oil polynomial defaults all-zero → first boot reads 0 bar, OilPrime aborts with no "uncalibrated" warning (cluster code 12 exists but is never raised) | [Config.cpp:211-214](src/system/Config.cpp) |
| ECU-LOG-03 | `partitions.csv` subtype `spiffs` for a LittleFS filesystem — latent on IDF upgrades | [partitions.csv:7](partitions.csv) |
| ECU-RTOS-02 | Watchdog armed *after* webTask spawn — Wi-Fi bring-up unprotected | [main.cpp:1822 vs 1839](src/main.cpp) |
| ECU-CTL-13 / LIM-04 | RPM+TOT pullbacks compound (−50%/tick worst case); pullback interaction with `skipSafetyChecks` undocumented | [ThrottleSlew.h:59-67](src/engine/controllers/ThrottleSlew.h) |
| ECU-BOOT-02 | Fuel solenoid default on GPIO 12 (MTDI strapping pin) | [hardware_profile.h:182](hardware_profile.h) |
| ECU-SEN-06 | `AnalogLinearSensor` extrapolates beyond calibration range without clamping (PolySensor clamps; Linear doesn't) | [AnalogSensor.h:119-124](src/hal/sensors/AnalogSensor.h) |
| ECU-WEB-14 | No CSRF protection — a malicious page on any device on the AP can auto-POST `/api/start` | all endpoints |

Also worth knowing: `SysMode::FAULT` is **never assigned anywhere** — the FAULT-mode branches in `main.cpp:1731` and Types.h are dead code. Either wire it up (e.g. profile mismatch currently uses `profileMatch` flags instead) or delete the mode.

The remaining ~28 still-present items are hygiene-level (stale comments, dead `_crc16`, missing teardown paths, type nits) — list in `docs/archive/audit-2026-06/*/bugs.md` cross-referenced by the verification.

---

## 6. Architecture assessment

The single-control-loop-on-Core-1 + everything-else-on-Core-0 model is **the right architecture for this scope**. No per-subsystem tasks means no priority-inversion surprises and the TWDT story is simple. The `volatile`-bus + one-way command queue is technically not C++-standard-compliant cross-core synchronization, but on Xtensa with both cores in coherent SRAM it works, the failure modes are understood and documented, and nothing safety-relevant crosses cores without the queue. I would not rewrite it; I *would* add a single `std::atomic<uint32_t>` sequence counter if you ever need consistent multi-field snapshots on Core 0.

Things I'd evolve:

- **Loop pacing:** the Core 1 loop is uncapped. All time-based code handles this correctly (dt-scaled), but it burns power, makes serial-blocking effects worse (F2), and makes loop-time regressions invisible. Add a loop-time monitor (max/avg µs in telemetry) and optionally a modest cap (e.g. 1 kHz).
- **`OT_DECLARE_HARDWARE`** instantiates ~80 globals including 3 variants per actuator role via one giant macro. It works and avoids heap, but it's the least maintainable part of the codebase. A table-driven registry would shrink Hardware.h dramatically.
- **Watchdog reboot mid-run:** TWDT panic → reboot → boot into STANDBY with all outputs off → fuel solenoid closes. That's a sane fail-state (flameout, not runaway), but it's worth documenting as the designed behavior, and the `resetReason` field you already log deserves a UI banner ("ECU rebooted unexpectedly during last run").

---

## 7. Documentation status

- **Current documentation note:** this section is retained as the 2026-06-10
  reviewer's assessment. README, CODEMAP, DESIGN_SPEC, and beta docs have been
  updated since this review.
- **CHANGELOG:** accurate for v1.1.0 but silent about the large post-audit remediation commit.
- **OTC protocol doc + example client:** verified accurate against `ClusterSerial.cpp`.

---

## 8. Security posture

The whole control plane trusts the Wi-Fi link. Current barriers: physical RF range + optional WPA2 password (off by default). Given this can start a jet engine and flash arbitrary unsigned firmware:

1. **Ship with a non-empty default AP password** (e.g. derived from chip ID, printed at boot on serial) — one-line change, removes the drive-by case.
2. Add a simple session token or PIN gate for the dangerous endpoints (`/api/start`, `/update`, `/api/factory_reset`, `/api/ecu_config` POST, dev/bench toggles). Doesn't need to be cryptographically fancy — it needs to stop CSRF and casual association.
3. Consider `Update.setMD5()`/size validation on OTA (currently `UPDATE_SIZE_UNKNOWN`, any payload accepted).
4. XSS: largely mitigated (good `textContent` discipline); fix the quote-only tooltip escaping.

---

## 9. Prioritized recommendations

**Before next live engine run**
1. Fix F1 (AB `Igniting` latch) and add the validator warning.
2. Fix F2 (LEDC serial flood) — one-line condition change.
3. Make calibration wizards STANDBY-gated with igniter confirm (F3).
4. Default-on AP password (§8.1).
5. Fix oil gauge red-state logic (F4).

**Next release**
6. Block START during extra cooldown (F7); clear all tool timers in `enterStandby` (F8); debounce start switch (F9).
7. Address the surviving audit criticals: oil failsafe floor-only semantics (CTL-03), `n1Healthy` gate on standby oil feed (LIM-14), AnalogLinear clamping (SEN-06).
8. Resolve the double-WebSocket design (F11) and bench-mode START confirm.
9. Regenerate gz assets + `.gitattributes` + sync check in CI (F5).
10. Set up CI: build both envs, fail on warnings, check data/ sync, flash-size budget alarm (you're at 90.6%).

**Ongoing**
11. Documentation cleanup was a follow-up item from this archived review; current release-facing docs have since been refreshed.
12. Decide the fate of `SysMode::FAULT` (wire up or delete).
13. Plan for flash headroom (trim, or 8 MB parts for new hardware).

---

## 10. Verdict

A reviewer's honest one-liner: **this codebase is in the top decile of open-source embedded projects I've seen for safety reasoning and code hygiene, held back by a fully-open network control plane and a handful of state-machine edge cases in the newest features (AB, extra cooldown, calibration UX).** The previous audit cycle demonstrably worked — most of what remains is either a deliberate accepted trade-off or new-feature edge cases like the ones above. Fix the five "before next live run" items and put a password on the AP, and I'd be comfortable with this controlling a bench engine.
