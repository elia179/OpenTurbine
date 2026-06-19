# Sequencer-state subsystem audit

## Files reviewed

- `src/engine/sequencer/SequenceEngine.h`
- `src/engine/sequencer/IBlock.h`
- `src/engine/sequencer/blocks/OilPrime.h`
- `src/engine/sequencer/blocks/StarterSpin.h`
- `src/engine/sequencer/blocks/PreIgnSpark.h`
- `src/engine/sequencer/blocks/FuelOpen.h`
- `src/engine/sequencer/blocks/FlameConfirm.h`
- `src/engine/sequencer/blocks/TempConfirm.h`
- `src/engine/sequencer/blocks/TimedDelay.h`
- `src/engine/sequencer/blocks/FuelPumpIdle.h`
- `src/engine/sequencer/blocks/ModifiedIdle.h`
- `src/engine/sequencer/blocks/Spool.h`
- `src/engine/sequencer/blocks/SafetyHold.h`
- `src/engine/sequencer/blocks/ImmediateCut.h`
- `src/engine/sequencer/blocks/RPMDrop.h`
- `src/engine/sequencer/blocks/CooldownSpin.h`
- `src/engine/sequencer/blocks/FinalStop.h`
- `src/engine/sequencer/blocks/ActuatorBlocks.h`
- `src/engine/sequencer/blocks/AdvancedBlocks.h`
- `src/engine/sequencer/blocks/MoreBlocks.h`
- `src/engine/sequencer/blocks/ABCheckReady.h`
- `src/engine/sequencer/blocks/ABIgnite.h`
- `src/engine/sequencer/blocks/ABFlameConfirm.h`
- `src/engine/sequencer/blocks/ABStabilize.h`
- `src/engine/sequencer/blocks/WaitForInput.h`
- `src/main.cpp` (lines 96-318, 644-843, 922-1108)

---

## Findings (Critical -> Info)

---

### ECU-SEQ-01: Double `onExit()` call when abort triggers shutdown sequence re-entry

- Severity: High
- Bug class: State machines / logic bugs
- Location: `src/engine/sequencer/SequenceEngine.h:87-105`, `src/main.cpp:1085-1096`
- Description: In `SequenceEngine::tick()`, the Abort path calls `_blocks[_idx]->onExit()` at line 99, then calls `_abort()` at line 103, and only THEN sets `_running = false` at line 104. The `_abort` callback is `enterAbortStandby()`. When `fuelEverOpened` is true (fuel was open during the aborted startup), `enterAbortStandby()` calls `g_sequencer.startSequence(_shutdownBlocks, _shutdownCount)`. Inside `startSequence()`, the guard at line 39 evaluates `_running` -- which is still `true` -- and calls `_blocks[_idx]->onExit()` a second time on the same block that already exited. Any block whose `onExit()` has side effects (e.g., `FlameConfirm` cuts igniter, `Spool` cuts starter, `OilPrime` sets `oilMinBar`) will have those effects applied twice in rapid succession on the same tick.
- Trigger: Startup abort (any block returning `BlockResult::Abort`) after fuel has been opened. Concretely: `TempConfirm` or `FlameConfirm` aborting on no-ignition timeout with `fuelEverOpened=true`.
- Impact: correctness, engine-safety (wrong actuator state during shutdown sequence entry)
- Evidence:
```cpp
// SequenceEngine.h:99-104
_blocks[_idx]->onExit();               // first call
if (_abort) _abort();                   // calls enterAbortStandby(), which calls startSequence()
_running = false;                       // too late -- startSequence() already saw _running=true

// startSequence():39-41
if (_running && _blocks && _idx < _count) {
    _blocks[_idx]->onExit();           // second call on the same block
}
```
- Suggested fix: In the Abort path, set `_running = false` BEFORE calling `_abort()`. To preserve `currentBlockName()` functionality, capture the name in a local variable before clearing `_running`:
```cpp
FlightRecorder::logBlockExit(_blocks[_idx]->name(), "abort");
_blocks[_idx]->onExit();
_running = false;             // clear before callback
if (_abort) _abort();
```
  Then update `enterAbortStandby()` to capture block name from the flight recorder or a pre-saved string rather than `g_sequencer.currentBlockName()`.
- Confidence: high

---

### ECU-SEQ-02: `SafetyHold` allows transition to RUNNING when N1 sensor is unhealthy

- Severity: High
- Bug class: Fail-safe logic / state machines
- Location: `src/engine/sequencer/blocks/SafetyHold.h:45-48`
- Description: The final pre-RUNNING checks are gated on `n1Healthy` and `oilHealthy`. If either sensor is unhealthy (flag false), the corresponding check is silently skipped and the engine transitions to RUNNING regardless of actual RPM or oil pressure. This is fail-open behavior: a broken N1 sensor at spool completion would cause the engine to enter RUNNING mode at unknown RPM. Once in RUNNING mode, the dynamic idle controller applies throttle demand with no RPM feedback, risking a runaway condition.
- Trigger: N1 RPM sensor faults or goes stale after `Spool` completes (unlikely but possible during a vibration-induced sensor dropout). Oil sensor failure during the safety hold dwell. Either condition allows entry into RUNNING.
- Impact: engine-safety (runaway fueling or undetected low-RPM transition to RUNNING)
- Evidence:
```cpp
// SafetyHold.h:45-48
if (!ed.benchMode) {
    if (ed.n1Healthy && ed.n1Rpm < finalCheckRpm) return BlockResult::Fault;   // skipped if !n1Healthy
    if (ed.oilHealthy && ed.oilPressure < runningOilMin) return BlockResult::Fault; // skipped if !oilHealthy
}
return BlockResult::Complete;  // proceeds unconditionally on unhealthy sensor
```
- Suggested fix: Treat an unhealthy N1 sensor at this stage as a fault condition rather than a pass-through. Add a dedicated check:
```cpp
if (!ed.benchMode) {
    if (!ed.n1Healthy) return BlockResult::Fault;   // sensor absent/faulted at final check = abort
    if (ed.n1Rpm < finalCheckRpm) return BlockResult::Fault;
    if (ed.oilHealthy && ed.oilPressure < runningOilMin) return BlockResult::Fault;
}
```
  If the engine is intentionally run without an N1 sensor (`!hw.hasN1Rpm`), `validateSequences` already flags SafetyHold as an error, so the guard is appropriate.
- Confidence: high

---

### ECU-SEQ-03: `FuelPulse::onExit()` is empty -- fuel solenoid left open if sequence is interrupted in phase 0

- Severity: High
- Bug class: State machines / actuator state undefined when onExit is skipped
- Location: `src/engine/sequencer/blocks/MoreBlocks.h:40`
- Description: `FuelPulse::onEnter()` opens the fuel solenoid immediately. The solenoid is closed inside `tick()` when the pulse timer expires (phase 0 -> phase 1 transition). However, `onExit()` is empty. If the sequence is interrupted while the block is in phase 0 (solenoid open, timer not yet expired) -- for example, when `startSequence()` is called from `enterShutdown()` or `enterFaultShutdown()` -- `onExit()` is invoked but does not close `fuelSolOpen`. The first block of the shutdown sequence (`ImmediateCut`) clears `fuelSolOpen` in its own `onEnter()`, so the exposure is limited to one `loop()` cycle. However, that cycle executes with the solenoid open and the fuel delivery committed to injectors.
- Trigger: Fault or manual shutdown command arriving while `FuelPulse` is executing its open pulse phase.
- Impact: engine-safety (brief uncommanded fuel injection during emergency shutdown entry)
- Evidence:
```cpp
// MoreBlocks.h:22-40
void onEnter() override {
    _entryMs = millis();
    _phase   = 0;
    EngineData::instance().fuelSolOpen = true;  // opened here
}
BlockResult tick() override {
    if (_phase == 0 && (now - _entryMs) >= pulseMs) {
        EngineData::instance().fuelSolOpen = false;  // only closed here, inside tick()
        ...
    }
}
void onExit() override {}   // does NOT close fuelSolOpen
```
- Suggested fix: Add solenoid close to `onExit()`:
```cpp
void onExit() override {
    EngineData::instance().fuelSolOpen = false;
}
```
- Confidence: high

---

### ECU-SEQ-04: `_abTriggerPrev` stale state causes immediate AB re-ignition on engine restart

- Severity: High
- Bug class: State machines / AB re-entry loop
- Location: `src/main.cpp:800-842`
- Description: `_abTriggerPrev` is a `static bool` local inside `checkABTrigger()`. It is updated (line 842) only when `ed.mode == SysMode::RUNNING`. When the engine is not in RUNNING mode, `checkABTrigger()` returns early and `_abTriggerPrev` is not updated. If the AB trigger (throttle threshold, switch, or RC input) remains asserted during the SHUTDOWN and STANDBY phases, then `_abTriggerPrev` retains its last RUNNING-mode value (true or false). On the next engine start, when mode transitions to RUNNING for the first time, `checkABTrigger()` is called with `triggerAsserted = true` (trigger still held) and `_abTriggerPrev` = whatever it was at the end of the previous RUNNING mode. If `_abTriggerPrev` happens to be false (trigger was released during RUNNING, then re-asserted during shutdown), `triggerRisingEdge = true` and `enterABIgniting()` is called on the very first RUNNING tick -- before the engine has fully stabilized.
- Trigger: Operator holds AB trigger through shutdown and restart with trigger still depressed. More likely on throttle-threshold source (source 1) where the throttle stick position is not actively managed between runs.
- Impact: engine-safety (uncommanded AB ignition attempt immediately after startup complete)
- Evidence:
```cpp
// main.cpp:752-757 -- early return skips _abTriggerPrev update
if (ed.mode != SysMode::RUNNING) {
    if (ed.abMode != ABMode::Off) { enterABShutdown(); }
    return;   // _abTriggerPrev NOT updated here
}
...
// main.cpp:800-801 -- rising edge computed from stale _abTriggerPrev
static bool _abTriggerPrev = false;
bool triggerRisingEdge = triggerAsserted && !_abTriggerPrev;
...
_abTriggerPrev = triggerAsserted;  // updated only when RUNNING
```
- Suggested fix: Update `_abTriggerPrev` before every early return so it always tracks the real trigger state:
```cpp
if (ed.mode != SysMode::RUNNING) {
    _abTriggerPrev = triggerAsserted;  // keep state current during non-RUNNING modes
    if (ed.abMode != ABMode::Off) { enterABShutdown(); }
    return;
}
```
  This requires evaluating `triggerAsserted` before the early return, which means moving the trigger-source switch block above the mode check, or duplicating the read.
- Confidence: high

---

### ECU-SEQ-05: Normal shutdown does not synchronously clear `abFuelOffset` when AB is Running

- Severity: Medium
- Bug class: State machines / actuator state undefined
- Location: `src/main.cpp:945-957`, `src/main.cpp:812-817`
- Description: `enterShutdown()` (normal operator stop) does not call `enterABShutdown()`. It sets `mode = SHUTDOWN` and starts the shutdown sequence. `abFuelOffset` is cleared only on the next call to `checkABTrigger()`, which runs later in the same `loop()` tick. However, `ImmediateCut::onEnter()` sets `throttleDemand = 0` immediately, and the throttle actuator output is `throttleDemand + abFuelOffset`. For the one tick between `enterShutdown()` and `checkABTrigger()` executing, the fuel actuator write is `0.0 + abFuelOffset` instead of zero. Compare with `enterFaultShutdown()` which correctly calls `enterABShutdown()` synchronously (line 968) before starting the shutdown sequence.
- Trigger: Operator commands a normal stop while AB is in `ABMode::Running`. Single-tick exposure.
- Impact: engine-safety (brief residual fuel delivery at AB offset level during first shutdown tick)
- Evidence:
```cpp
// main.cpp:945-957 -- enterShutdown() has no enterABShutdown() call
static void enterShutdown() {
    ...
    ed.mode = SysMode::SHUTDOWN;
    // No enterABShutdown() here -- contrast with enterFaultShutdown() at line 968
    g_sequencer.startSequence(_shutdownBlocks, _shutdownCount);
}

// main.cpp:959-971 -- enterFaultShutdown() correctly clears AB first
static void enterFaultShutdown() {
    ...
    if (HardwareConfig::hasAfterburner) enterABShutdown();  // synchronous clear
    g_sequencer.startSequence(_shutdownBlocks, _shutdownCount);
}
```
- Suggested fix: Mirror `enterFaultShutdown()` in `enterShutdown()`:
```cpp
static void enterShutdown() {
    ...
    ed.mode = SysMode::SHUTDOWN;
    if (HardwareConfig::hasAfterburner) enterABShutdown();  // add this line
    g_sequencer.startSequence(_shutdownBlocks, _shutdownCount);
}
```
- Confidence: high

---

### ECU-SEQ-06: `FlameConfirm` in bench mode returns `Complete` without clearing `seqWaitReason`

- Severity: Low
- Bug class: Logic bugs / state machine hygiene
- Location: `src/engine/sequencer/blocks/FlameConfirm.h:29-32`
- Description: In bench mode, `FlameConfirm::tick()` returns `BlockResult::Complete` immediately without calling `clearWaitReason()`. This leaves `ed.seqWaitReason` containing whatever string was last written (from a prior block or the initial empty string). The web UI reads `seqWaitReason` for display and may show stale text during bench runs. All other early-return Complete paths in this block do call `clearWaitReason()`.
- Trigger: Bench mode enabled, `FlameConfirm` reached in sequence.
- Impact: maintainability (stale status text on dashboard during bench runs)
- Evidence:
```cpp
// FlameConfirm.h:29-32
if (ed.benchMode) {
    Serial.println("[FlameConfirm] BENCH: simulating flame confirm");
    return BlockResult::Complete;  // clearWaitReason() not called
}
```
- Suggested fix:
```cpp
if (ed.benchMode) {
    Serial.println("[FlameConfirm] BENCH: simulating flame confirm");
    clearWaitReason();
    return BlockResult::Complete;
}
```
- Confidence: high

---

### ECU-SEQ-07: `ABIgnite::torchDurationMs` is `int` but compared against `unsigned long elapsed`

- Severity: Low
- Bug class: Integer issues / signed-unsigned mismatch
- Location: `src/engine/sequencer/blocks/ABIgnite.h:24`, `src/engine/sequencer/blocks/ABIgnite.h:78`
- Description: `torchDurationMs` is declared as `int` (default 400). The comparison `elapsed >= (unsigned long)torchDurationMs` casts a signed `int` to `unsigned long`. If `torchDurationMs` is misconfigured to a negative value (e.g., via a bad JSON value or a future API call), the cast produces a very large `unsigned long` (~4.29 billion for -1), causing `ABIgnite` to run the torch spike indefinitely -- never timing out. With `torchTotLimit` disabled (0), there is no secondary cut. The result is continuous fuel enrichment through the turbine until the AB sequence is externally aborted.
- Trigger: `Config::abTorchDurationMs` set to a negative value. Config parsing currently uses `| defaultValue` which would keep the default if the JSON value is absent, but a deliberately malformed value passes through as-is.
- Impact: engine-safety (runaway AB torch fuel enrichment if misconfigured)
- Evidence:
```cpp
// ABIgnite.h:24
int   torchDurationMs = 400;
// ABIgnite.h:78
if (elapsed >= (unsigned long)torchDurationMs) {  // negative int -> enormous unsigned long
```
- Suggested fix: Change `torchDurationMs` to `unsigned long` (matching the pattern used by all other timer fields), or add an explicit non-negative clamp in `applyConfig()`:
```cpp
g_blkABIgnite.torchDurationMs = (unsigned long)max(0, Config::abTorchDurationMs);
```
- Confidence: medium

---

### ECU-SEQ-08: `ABFlameConfirm` fault-on-timeout in mode 2 (timed) if `assumeIgnitedMs >= flameTimeoutMs`

- Severity: Medium
- Bug class: Logic bugs / AB state machine
- Location: `src/engine/sequencer/blocks/ABFlameConfirm.h:49-86`
- Description: The overall timeout check at line 50 (`if (elapsed >= flameTimeoutMs)`) fires before the mode-2 success check at line 81 (`if (elapsed >= assumeIgnitedMs)`). If `assumeIgnitedMs >= flameTimeoutMs`, the timeout branch is reached first and returns `BlockResult::Fault`, preventing AB from ever confirming in timed mode. This produces a Fault that puts `abMode = ABMode::Fault` -- requiring trigger release and re-arm to retry. Default values (`assumeIgnitedMs=1500`, `flameTimeoutMs=3000`) are safe, but if a user sets `assumeIgnitedMs=3000` and `flameTimeoutMs=3000` (equal), the timeout fires first due to `>=` on both sides in the same tick.
- Trigger: User configures `abAssumeIgnitedMs >= abFlameTimeoutMs` in engine settings.
- Impact: correctness (AB always faults in timed mode under this config; no visible error)
- Evidence:
```cpp
// ABFlameConfirm.h:50-86
if (elapsed >= (unsigned long)flameTimeoutMs) {
    return BlockResult::Fault;      // checked first
}
switch (flameMode) {
    case 2:
        if (elapsed >= (unsigned long)assumeIgnitedMs) {
            return BlockResult::Complete;  // never reached if assumeIgnitedMs >= flameTimeoutMs
        }
}
```
- Suggested fix: Check mode-2 success before the overall timeout, or add a validation warning in `validateSequences()` when `abAssumeIgnitedMs >= abFlameTimeoutMs`.
- Confidence: high

---

### ECU-SEQ-09: `buildSequences()` silently shortens sequence if a block name is not in registry

- Severity: Medium
- Bug class: Input validation / error handling
- Location: `src/main.cpp:99-135`
- Description: When `buildSequences()` iterates the configured block names, any name not found in `_blockRegistry` is silently skipped -- `_startupCount` (or equivalent) is not incremented for that entry. The `validateSequences()` call afterward does flag the unknown name as an error, but this error is only written to `ed.seqIssues`. The START command is blocked only if `ed.seqHasErrors && !ed.benchMode`. In bench mode, an unknown block name is simply missing from the sequence with no runtime indication. More dangerously, the shortened sequence may omit a critical safety check (e.g., a misspelled `FlameConfirm` block causes no flame detection), and bench mode runs would proceed through without it.
- Trigger: Typo in `hardware.json` sequence array (e.g., `"FlameConfirn"` instead of `"FlameConfirm"`). Bench mode enabled.
- Impact: engine-safety in bench mode; correctness in all modes (silent sequence truncation)
- Evidence:
```cpp
// main.cpp:99-106
for (int i = 0; i < hw.startupSeqLen; i++) {
    for (size_t j = 0; j < _blockRegistryLen; j++) {
        if (strcmp(_blockRegistry[j].name, hw.startupSeq[i]) == 0) {
            _startupBlocks[_startupCount++] = _blockRegistry[j].blk;
            break;
        }
    }
    // No else: unknown name -> silently skipped, _startupCount not incremented
}
```
- Suggested fix: After `buildSequences()`, assert that `_startupCount == hw.startupSeqLen` minus the count of known-unknown names. For unknown names, log at Serial level and set `seqHasErrors = true` unconditionally (already done in `validateSequences` for normal mode but not surfaced in bench mode). Consider refusing bench mode start if a safety-critical block name cannot be resolved.
- Confidence: high

---

### ECU-SEQ-10: `OilPrime::onExit()` sets `oilMinBar` on the Abort path, briefly arming oil safety monitor before `enterStandby()` resets it

- Severity: Low
- Bug class: State machines / actuator state on abort
- Location: `src/engine/sequencer/blocks/OilPrime.h:70-73`
- Description: `OilPrime::onExit()` unconditionally sets `ed.oilMinBar = oilArmMinBar` (default 1.5 bar). This is intended as a handoff to `StarterSpin`, which immediately overrides it. However, on the Abort path (oil prime timeout), `onExit()` is still called with the same side effect: `oilMinBar` is set to 1.5 bar. Immediately after, `enterAbortStandby()` -> `enterStandby()` resets `oilMinBar = 0`. The safety monitor (`SafetyMonitor::tick()`) runs once per loop() cycle and may evaluate `oilMinBar` and `oilPressure` in the brief window between `onExit()` and `enterStandby()`. If oil pressure is still below 1.5 bar at abort (which it is, since that is why we aborted), the monitor may log an additional spurious `LOW_OIL` fault event on top of the abort.
- Trigger: OilPrime block aborts due to timeout (oil never reached target pressure).
- Impact: stability / logging noise (spurious fault event in flight log, incorrect telemetry)
- Evidence:
```cpp
// OilPrime.h:70-73
void onExit() override {
    // Arm oil safety check — sequencer sets the real threshold in StarterSpin
    EngineData::instance().oilMinBar = oilArmMinBar;  // set unconditionally, even on abort
}
```
- Suggested fix: Only arm the oil safety threshold if the block completed successfully. One approach is to pass a flag or check a local variable set in `tick()` on `Complete`:
```cpp
void onExit() override {
    if (_completed) EngineData::instance().oilMinBar = oilArmMinBar;
}
// In tick(): set _completed = true before returning BlockResult::Complete
```
- Confidence: medium

---

### ECU-SEQ-11: `ABStabilize::onExit()` sets `abMode = Running` even when the block exits via Fault

- Severity: Medium
- Bug class: State machines / wrong state on fault exit
- Location: `src/engine/sequencer/blocks/ABStabilize.h:42-46`
- Description: `ABStabilize::onExit()` unconditionally sets `EngineData::instance().abMode = ABMode::Running`. The `SequenceEngine` calls `onExit()` on both `Complete` and `Fault` paths (and on sequence interruption via `startSequence()`). If `ABStabilize::tick()` returns `BlockResult::Fault` (TOT limit exceeded, line 33), `onExit()` is called and sets `abMode = Running` before `_fault()` (`abSequenceFault()`) is called. `abSequenceFault()` then immediately overwrites `abMode = ABMode::Fault`. The net result is `abMode` briefly flickers through `Running` before settling at `Fault`. In a concurrent environment (Core 0 reading `abMode` for web display), the web UI could observe a transient `Running` state for one inter-core read.
- Trigger: `ABStabilize` faults on TOT limit exceeded (line 30-34). `abMode` briefly = Running before abSequenceFault overrides it.
- Impact: correctness (momentary false `Running` state visible to web UI / telemetry)
- Evidence:
```cpp
// ABStabilize.h:30-46
BlockResult tick() override {
    if (stabilizeMaxTot > 0 && ed.totHealthy && ed.tot > stabilizeMaxTot) {
        return BlockResult::Fault;  // will trigger onExit() then _fault()
    }
    ...
}
void onExit() override {
    EngineData::instance().abMode = ABMode::Running;  // always set, including on Fault path
    Serial.println("[AB] Stabilize: complete — AB RUNNING");
}
```
- Suggested fix: Track completion success in a member flag and gate the state transition:
```cpp
private: bool _completed = false;
// In tick(): _completed = true; return BlockResult::Complete;
void onExit() override {
    if (_completed) {
        EngineData::instance().abMode = ABMode::Running;
    }
}
```
  Reset `_completed = false` in `onEnter()`.
- Confidence: high

---

### ECU-SEQ-12: `WaitForInput` with `timeoutMs == 0` and invalid `channelIdx` hangs permanently

- Severity: Low
- Bug class: Input validation / stuck states
- Location: `src/engine/sequencer/blocks/WaitForInput.h:43-53`
- Description: `WaitForInput::tick()` reads `ed.diState[channelIdx]` with an in-bounds check for `channelIdx >= 0 && channelIdx < HardwareConfig::MAX_DI`, returning `false` when out-of-range. If `expectedState = true` (wait until active) and `channelIdx` is invalid, `state` is always `false != true`, and with `timeoutMs = 0` (wait forever), the block returns `Running` indefinitely. The startup or AB sequence hangs with no fault and no abort. On a turbine, this means the engine is left in whatever state the prior block established (igniter on, starter spinning, fuel open) with no way to progress or abort automatically.
- Trigger: Invalid `channelIdx` in hardware.json (e.g., index 4 on a 4-channel system with `MAX_DI=4`) combined with `timeoutMs=0`.
- Impact: engine-safety (sequence permanently stuck; actuators in mid-startup state; no fuel/igniter cutoff)
- Evidence:
```cpp
// WaitForInput.h:43-53
bool state = (channelIdx >= 0 && channelIdx < HardwareConfig::MAX_DI)
             ? ed.diState[channelIdx] : false;   // invalid index -> always false

if (state == expectedState) return BlockResult::Complete;
if (timeoutMs > 0 && (millis() - _entryMs) >= timeoutMs) return BlockResult::Abort;
return BlockResult::Running;  // infinite loop when index invalid and timeoutMs==0
```
- Suggested fix: Treat an invalid `channelIdx` as an immediate Abort in both `onEnter()` (log the error) and `tick()` (return `Abort`). Add a validation check to `validateSequences()` for `WaitForInput` blocks.
- Confidence: high

---

## Notes / unclear areas

1. **`enterAbortStandby` block-name capture timing**: The comment on `SequenceEngine.h:100-102` correctly explains why `_abort()` is called before `_running = false` -- to allow `currentBlockName()` to work. If ECU-SEQ-01 is fixed by moving `_running = false` before `_abort()`, a different mechanism is needed to pass the block name to `enterAbortStandby()`. Consider saving the name to a module-level `char[]` before clearing `_running`.

2. **`FuelPulse` + `fuelEverOpened`**: The comment in MoreBlocks.h explicitly states `fuelEverOpened is NOT set` for `FuelPulse`. This means a sequence using only `FuelPulse` (no `FuelOpen`) as its fuel delivery step will not trigger the shutdown path in `enterAbortStandby`, even if actual combustion occurred. This may be intentional (pre-ignition prime only), but should be verified against any sequences that use `FuelPulse` as the sole ignition-phase fuel command.

3. **`RPMDrop` fails open on sensor fault**: `RPMDrop::tick()` completes if `!ed.n1Healthy`. This is intentional (sensor fault = proceed with cooldown anyway), but means an N1 sensor that faults at high RPM would immediately advance to `CooldownSpin` without waiting for actual spindown. `CooldownSpin` would then apply starter for cooling while the turbine may still be at full speed, causing overcurrent on the starter. This is a pre-existing design trade-off; flagging for awareness.

4. **Single-instance block singletons**: All blocks are global singletons declared via `OT_DECLARE_HARDWARE`. The same instance is shared between startup, shutdown, and AB sequences. If a sequence is interrupted mid-block and a new sequence begins reusing the same block (e.g., `TimedDelay` in both startup and AB sequences), the `_entryMs` is reset in `onEnter()`. This is safe as designed, but becomes fragile if `g_sequencer` and `g_abSequencer` ever share the same block object concurrently.

5. **`GlowPreheat` overflow potential**: `(millis() - _startMs) < preheatMs + waitHotTimeout` -- if both `preheatMs` and `waitHotTimeout` are set to values near `ULONG_MAX/2` by a malicious or corrupted config, the sum overflows and the condition inverts, causing the block to complete immediately. Unlikely in practice but worth noting.
