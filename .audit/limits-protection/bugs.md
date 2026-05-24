# Limits-protection subsystem audit

## Files reviewed

| File | Lines examined |
|---|---|
| `src/engine/SafetyMonitor.h` | full (363 lines) |
| `src/system/RulesEngine.h` | full (81 lines) |
| `src/engine/EngineData.h` | full (228 lines) |
| `src/system/Config.h` | full (349 lines) |
| `src/system/Config.cpp` | selected (defaults, _fromDoc) |
| `src/engine/controllers/DynamicIdle.h` | full (113 lines) |
| `src/engine/controllers/ThrottleSlew.h` | full (88 lines) |
| `src/engine/sequencer/blocks/CooldownSpin.h` | full (90 lines) |
| `src/Hardware.h` | applyConfig (396-521), updateActuators (975-1131), allOff (1099-1131) |
| `src/main.cpp` | lines 411-642, 845-920, 959-1050, 1100-1200, 1510-1600, 1618-1690 |

---

## Findings

### ECU-LIM-01: NULL dereference in enterFaultShutdown when lastFault() is null
- **Severity:** Critical
- **Bug class:** Error handling / NULL dereference
- **Location:** `src/main.cpp:961-975`, `src/engine/SafetyMonitor.h:278`

**Description:**
`_lastFault` is initialized to `nullptr` in `SafetyMonitor` (line 278). `enterFaultShutdown()` reads it unconditionally at line 961:
```c
const char* fault = g_safety.lastFault();   // may be nullptr
snprintf(ed.lastEvent, sizeof(ed.lastEvent), "FAULT: %s", fault);  // UB if nullptr
```
`snprintf` with a null `%s` argument is undefined behaviour on ESP-IDF / newlib; in practice it crashes or prints "(null)". The `strcmp` calls on lines 975-978 also dereference `fault` without a null guard (the MAVLink path at line 983 has a guard `fault ? fault : "?"` but the `snprintf` at 969 and the `strcmp` chain do not).

**Trigger:**
Any code path that calls `enterFaultShutdown()` without first calling `_trigger()` or `setExternalFault()`. The DI `estop` role (line 610) calls `enterShutdown()`, not `enterFaultShutdown()`, but a future role addition or a direct call to `enterFaultShutdown` from a new abort path would trigger this immediately.

**Impact:**
ESP32 crash / panic during fault shutdown -- the very moment protection is most needed.

**Evidence:**
- `SafetyMonitor.h:278`: `const char* _lastFault = nullptr;`
- `main.cpp:961`: `const char* fault = g_safety.lastFault();` -- no null check before use
- `main.cpp:969`: `snprintf(ed.lastEvent, sizeof(ed.lastEvent), "FAULT: %s", fault);`
- `main.cpp:975`: `strcmp(fault, "OVERSPEED")` -- dereferences fault

**Suggested fix:**
Add a null guard immediately after line 961:
```c
const char* fault = g_safety.lastFault();
if (!fault) fault = "UNKNOWN";
```
Alternatively, initialize `_lastFault = "UNKNOWN"` in `SafetyMonitor`.

**Confidence:** High

---

### ECU-LIM-02: Cooldown skip (START+STOP) has no TOT guard -- hot turbine can be sent to STANDBY immediately
- **Severity:** Critical
- **Bug class:** Fail-safe logic / state machine
- **Location:** `src/main.cpp:850-871`

**Description:**
`checkCooldownSkip()` calls `enterStandby()` directly when both buttons are held for `cooldownSkipHoldMs` (default 1 second). `enterStandby()` zeroes all actuators including the oil pump and starter motor but does not consult `ed.tot` or `Config::totCooldownTarget`. The CooldownSpin block (which enforces `tot < totTarget` before exiting) is bypassed entirely.

A turbine shut down with TOT of, say, 600 degC and immediately forced to STANDBY will sit with no airflow and no oil circulation. The hot-start protection will block the _next_ start attempt (if `hotStartTotThreshold > 0`) but the bearings, turbine wheel, and combustor liner will soak in retained heat with no cooling.

**Trigger:**
Operator holds START+STOP for 1 second at any point during SHUTDOWN (including immediately after cutting fuel, before CooldownSpin has run). No minimum TOT requirement.

**Impact:**
Bearing and turbine wheel heat-soak damage; potential coking of oil lines. Severity escalates with higher TOT at the time of skip.

**Evidence:**
- `main.cpp:862-868`: hold time check calls `enterStandby()` with no TOT check
- `CooldownSpin.h:73`: only the block itself checks `ed.tot < totTarget`; cooldown skip completely bypasses it
- `Config.cpp:9`: default `totCooldownTarget = 150` (not consulted by cooldown skip)

**Suggested fix:**
In `checkCooldownSkip()`, gate the `enterStandby()` call:
```c
if (ed.totHealthy && ed.tot > Config::totCooldownTarget) {
    Serial.println("[OT] Cooldown skip blocked — TOT still above safe level");
    _cooldownSkipHoldStart = 0;
    return;
}
```
If `totHealthy` is false, allow the skip but log a warning.

**Confidence:** High

---

### ECU-LIM-03: RulesEngine runs during SHUTDOWN and FAULT modes and can reopen bleed valve / fuel pump 2
- **Severity:** High
- **Bug class:** Fail-safe logic / state machine
- **Location:** `src/system/RulesEngine.h:27-41`, `src/main.cpp:1657-1659`

**Description:**
`RulesEngine::evaluate()` suppresses execution only for `STANDBY` and `benchMode`. It runs without restriction in `STARTUP`, `RUNNING`, `SHUTDOWN`, and `FAULT` (line 30). The main loop calls it at line 1659, after `g_safety.check()` and all controller writes, but before `Hardware::updateActuators()`.

If a rule is configured as "if TOT > X then FUEL_PUMP2 on" (a plausible enrichment rule), the rule will keep `fuelPump2Demand` non-zero through the shutdown sequence, fighting the sequencer's intent to cut fuel. Similarly, a "if N1 > Y then BLEED_VALVE open" rule will keep the bleed valve open during and after fault shutdown. `allOff()` in `enterStandby()` zeroes `fuelPump2Demand` and de-energises the actuator, but the _tick following_ `enterStandby()` will re-evaluate the rule if the sensor condition is still met and restore the demand before `updateActuators()` writes it to hardware.

**Trigger:**
Any rule whose sensor condition remains true during SHUTDOWN or FAULT (e.g. high TOT persists after flame-out, high N1 during spin-down).

**Impact:**
- FUEL_PUMP2 running during shutdown can sustain combustion if the solenoid was not fully closed upstream.
- BLEED_VALVE held open during shutdown affects compressor surge recovery.
- Neither is a common configuration, but the architecture provides no protection against it.

**Evidence:**
- `RulesEngine.h:30`: only `STANDBY` and `benchMode` are suppressed
- `RulesEngine.h:75-77`: `_applyActuator` writes to `ed.coolFanOn`, `ed.bleedValveOpen`, `ed.fuelPump2Demand` unconditionally
- `main.cpp:1657-1661`: `RulesEngine::evaluate()` then `Hardware::updateActuators()` -- rules win the last write

**Suggested fix:**
Add `SHUTDOWN` and `FAULT` to the suppress list:
```c
if (ed.mode == SysMode::STANDBY ||
    ed.mode == SysMode::SHUTDOWN ||
    ed.mode == SysMode::FAULT   ||
    ed.benchMode) return;
```

**Confidence:** High

---

### ECU-LIM-04: ThrottleSlew safety pullback is NOT suppressed by skipSafetyChecks -- asymmetric bypass
- **Severity:** High
- **Bug class:** Fail-safe logic / state machine
- **Location:** `src/engine/controllers/ThrottleSlew.h:47-58`, `src/engine/SafetyMonitor.h:55`

**Description:**
`SafetyMonitor::check()` returns immediately when `ed.skipSafetyChecks || ed.benchMode` (line 55). However, `ThrottleSlew::tick()` contains its own soft-limit pullback logic (RPM and TOT approach limiters, lines 50-57) that has no such bypass. In DEV_MODE with safety checks skipped, the operator expects unrestricted throttle authority, but ThrottleSlew will still reduce throttle demand when N1 approaches `rpmSoftLimit` (95% of rpmLimit by default).

The inverse problem is more safety-critical: ThrottleSlew's soft-limit pullback is the _only_ continuously-acting fuel-reduction mechanism between the overspeed soft threshold (95% of limit) and the hard overspeed trip (100%). If `skipSafetyChecks` is set, the hard trip is also suppressed, making the ThrottleSlew pullback the sole protection during development runs near the overspeed threshold.

**Trigger:**
DEV_MODE with `skipSafetyChecks` enabled, engine operated near rpmLimit.

**Impact:**
During development testing: operator experiences unexpected throttle restriction when they believe safety is bypassed. More critically: the hard overspeed shutdown is gone but the soft pullback remains, giving a false impression that some protection exists when in fact the behaviour is undefined -- ThrottleSlew was not designed to be the primary protection layer.

**Evidence:**
- `SafetyMonitor.h:55`: full bypass on `skipSafetyChecks || benchMode`
- `ThrottleSlew.h:50-52`: RPM pullback with no `skipSafetyChecks` check
- `Hardware.h:484-487`: ThrottleSlew is configured with `rpmHardLimit = Config::rpmLimit` and `rpmSoftLimit = Config::rpmLimit * 0.95f` independently of SafetyMonitor

**Suggested fix:**
Either document that ThrottleSlew pullback is always active (add a comment in the DEV_MODE warning), or gate it:
```c
if (!ed.skipSafetyChecks) {
    // RPM pullback ...
    // TOT pullback ...
}
```

**Confidence:** High

---

### ECU-LIM-05: Limp mode throttle cap cannot override DynamicIdle minimum floor
- **Severity:** High
- **Bug class:** Logic bug / fail-safe
- **Location:** `src/main.cpp:1638-1645`, `src/engine/controllers/DynamicIdle.h:77-99`

**Description:**
The limp-mode throttle cap is applied after `Hardware::runControllers()` (which runs DynamicIdle):
```c
if (ed.limpMode && ed.mode == SysMode::RUNNING) {
    float cap = Config::limpMaxThrottlePct / 100.0f;   // default 0.50
    if (ed.throttleDemand > cap) ed.throttleDemand = cap;
}
```
DynamicIdle computes a `minFloor`:
```c
float minFloor = (targetRpm * minMultiplier) / rpmLimit;
```
With defaults `targetRpm=44000`, `minMultiplier=0.75`, `rpmLimit=60000` (the DynamicIdle disengage ceiling, not the safety limit), `minFloor = 0.55`. The limp cap (default 0.50) is **below** `minFloor`. DynamicIdle then unconditionally raises `throttleDemand` to `minFloor` if it is below that value (line 98-99: `if (ed.throttleDemand < demand) ed.throttleDemand = demand`).

However, limpMode cap runs _after_ DynamicIdle, so the cap wins the last write to `throttleDemand` for this tick. The problem is the next tick: DynamicIdle runs again and will raise `throttleDemand` back up to `minFloor` before the limp cap gets another chance. The net result depends on run order, but the intent -- hold throttle below the limp cap -- is violated every tick by the floor raise followed by the cap write, producing oscillation at the cap boundary rather than a stable reduced throttle.

**Trigger:**
`limpMode` active, `DynamicIdle` active, `limpMaxThrottlePct < (targetRpm * minMultiplier / idleRpmLimit * 100)`.

**Impact:**
RPM sensor lost (the primary limpMode trigger from line 252 of SafetyMonitor) combined with active DynamicIdle creates an uncontrolled idle floor fight. Engine idles higher than intended in limp, defeating the conservative power reduction goal.

**Evidence:**
- `main.cpp:1638-1645`: limp cap applied last in loop
- `DynamicIdle.h:77`: `minFloor` computed from config values
- `DynamicIdle.h:95-99`: floor is a hard minimum raise, not suppressed by limpMode
- `main.cpp:1636`: `Hardware::runControllers()` (DynamicIdle) runs before limpMode cap

**Suggested fix:**
In `DynamicIdle::tick()`, check `ed.limpMode` and either disengage entirely or respect the cap:
```c
if (ed.limpMode) { _idleFloor = 0; _integrator = 0; return; }
```
Or clip `minFloor` to the limp cap when limp is active.

**Confidence:** High

---

### ECU-LIM-06: flameoutShutdownMs stored as float; cast to unsigned long is UB if negative
- **Severity:** Medium
- **Bug class:** Integer issue / type safety
- **Location:** `src/engine/SafetyMonitor.h:34,145`

**Description:**
`flameoutShutdownMs` is declared `float` (line 34). Line 145 casts it to `unsigned long`:
```c
if ((now - _flameoutMs) > (unsigned long)flameoutShutdownMs) {
```
If Config delivers a negative value (e.g. a user types `-1` in the web UI, ArduinoJson parses it as -1.0f, and `Config::flameoutShutdownMs` has no lower-bound validation), the cast `(unsigned long)(-1.0f)` is undefined behaviour in C++ (the value is out of range of `unsigned long`). On ESP32/Xtensa GCC, the result is typically 0, which would make the condition immediately true on the first check, triggering a fault within 100 ms of the first flameless reading.

**Trigger:**
Config JSON contains `"flameout_shutdown_ms": -1` or any negative value.

**Impact:**
Immediate phantom FLAMEOUT fault 100 ms after first non-flame reading. False shutdown of running engine.

**Evidence:**
- `SafetyMonitor.h:34`: `float flameoutShutdownMs = 3000.0f;`
- `SafetyMonitor.h:145`: `(unsigned long)flameoutShutdownMs`
- `Config.cpp:601`: no lower-bound validation on `flameoutShutdownMs` after JSON load

**Suggested fix:**
Validate in `applyConfig()`:
```c
g_safety.flameoutShutdownMs = max(Config::flameoutShutdownMs, 500.0f);
```
Or change the field type to `unsigned long` and validate on JSON parse.

**Confidence:** High

---

### ECU-LIM-07: safetyCheckIntervalMs type mismatch (int Config -> unsigned long SafetyMonitor) can produce ~49-day interval if negative
- **Severity:** Medium
- **Bug class:** Integer issue / type mismatch
- **Location:** `src/system/Config.h:82`, `src/engine/SafetyMonitor.h:35`, `src/Hardware.h:520`

**Description:**
`Config::safetyCheckIntervalMs` is declared `static int` (Config.h:82). `SafetyMonitor::checkIntervalMs` is `unsigned long` (SafetyMonitor.h:35). Assignment in `applyConfig()`:
```c
g_safety.checkIntervalMs = Config::safetyCheckIntervalMs;   // implicit signed→unsigned
```
If the config JSON sets `check_interval_ms` to a negative value (e.g. `-1`), the int value -1 is assigned to unsigned long, producing 0xFFFFFFFF (4,294,967,295 ms, approximately 49 days). The interval gate at SafetyMonitor.h:93:
```c
if (now - _lastCheckMs < checkIntervalMs) return;
```
will never pass (since `millis()` wraps at ~49 days, but the subtraction in unsigned arithmetic will stay well below the huge interval for the entire device lifetime). All interval-gated safety checks (overtemp, oil, flameout, underspeed, surge) are silently disabled for the session.

**Trigger:**
Config JSON `"check_interval_ms": -1` or any negative value. No lower-bound validation exists in `_fromDoc`.

**Impact:**
All interval-gated safety checks disabled. OVERTEMP, LOW_OIL, FLAMEOUT, UNDERSPEED, SURGE cannot fire. Engine can self-destruct.

**Evidence:**
- `Config.h:82`: `static int safetyCheckIntervalMs;`
- `SafetyMonitor.h:35`: `unsigned long checkIntervalMs = 100;`
- `Hardware.h:520`: implicit narrowing/sign conversion assignment
- `Config.cpp:601`: no validation after JSON load

**Suggested fix:**
Clamp in `applyConfig()`:
```c
g_safety.checkIntervalMs = (unsigned long)max(Config::safetyCheckIntervalMs, 10);
```

**Confidence:** High

---

### ECU-LIM-08: setExternalFault stores a pointer into HardwareConfig memory; safe but fragile contract
- **Severity:** Medium
- **Bug class:** Memory safety / pointer lifetime
- **Location:** `src/engine/SafetyMonitor.h:49`, `src/main.cpp:601-604`

**Description:**
`setExternalFault(const char* code)` stores the raw pointer:
```c
void setExternalFault(const char* code) { _lastFault = code; }
```
At the call site (main.cpp:601):
```c
const char* diCode = hw.diCh[i].faultCode[0] ? hw.diCh[i].faultCode : "DI_FAULT";
g_safety.setExternalFault(diCode);
```
`hw.diCh[i].faultCode` is a `char[24]` field inside `HardwareConfig` (a singleton with static storage). The string literal `"DI_FAULT"` is also in rodata. Both are stable. However, the contract is undocumented. If a future contributor passes a stack-local buffer (e.g. a `char buf[32]` constructed in a handler), `_lastFault` will point to freed stack memory at the time `enterFaultShutdown()` is called. `enterFaultShutdown` is called _immediately_ after `setExternalFault` on line 604, so in the current code the lifetime is safe. But the API signature accepts any `const char*` with no documentation of the lifetime requirement.

**Trigger:**
A future code change passes a stack-allocated string to `setExternalFault()`.

**Impact:**
Use-after-free: `lastFault()` returns a dangling pointer, producing garbage fault codes in the flight log and potentially a crash in `enterFaultShutdown()` during strcmp chain.

**Evidence:**
- `SafetyMonitor.h:49`: stores raw pointer
- `main.cpp:601-604`: current call site is safe (HardwareConfig is static)
- No documentation on pointer lifetime requirement

**Suggested fix:**
Change `_lastFault` to a `char` array and copy in `setExternalFault`:
```c
char _lastFaultBuf[32] = {};
void setExternalFault(const char* code) {
    strncpy(_lastFaultBuf, code, sizeof(_lastFaultBuf) - 1);
    _lastFaultBuf[sizeof(_lastFaultBuf) - 1] = '\0';
    _lastFault = _lastFaultBuf;
}
```

**Confidence:** Medium (current instantiation is safe; risk is latent)

---

### ECU-LIM-09: Surge buffer not reset when transitioning RUNNING -> SHUTDOWN (stale RPM samples carried into next startup)
- **Severity:** Medium
- **Bug class:** State machine / surge detection
- **Location:** `src/engine/SafetyMonitor.h:59-73,210-239`

**Description:**
The surge circular buffer (`_n1Buf`, `_n1BufIdx`, `_n1BufCount`) is only reset on `STANDBY` entry (lines 68-69). The comment at line 65 explains this is intentional to avoid wiping the buffer "mid-spindown," but there is a side effect: if a fault shutdown occurs during RUNNING (e.g. OVERSPEED), the mode goes to SHUTDOWN. On the next startup attempt (STANDBY -> STARTUP -> RUNNING), the buffer still holds the last 10 RPM samples from the _previous_ run. The first time the surge check runs in the new RUNNING session it will have a full buffer of potentially high-variance samples from the previous spindown, and can immediately assert `surgeDetected` and trigger "SURGE" false fault.

**Trigger:**
Fault shutdown from RUNNING (buffer has 10 samples), engine cooled and restarted without entering a long STANDBY period. Surge detection enabled (`surgeRpmVariance > 0`).

**Impact:**
False SURGE fault immediately after entering RUNNING on the new start. Engine shut down erroneously.

**Evidence:**
- `SafetyMonitor.h:67-70`: buffer reset only on `SysMode::STANDBY`
- `SafetyMonitor.h:210-239`: surge check runs as soon as `_n1BufCount >= SURGE_BUF` (10 samples); if buffer was pre-filled it fires on tick 1 of new RUNNING
- The spindown comment at line 65 acknowledges the tradeoff but does not account for the next-startup contamination

**Suggested fix:**
Also reset the surge buffer on `RUNNING` entry (i.e., in `enterRunning()` or in `begin()`), or at minimum at any transition _into_ STARTUP:
```c
// In SafetyMonitor::check(), in the !inOp branch, also clear on STARTUP
if (m == SysMode::STARTUP || m == SysMode::STANDBY) {
    _n1BufIdx = 0; _n1BufCount = 0;
}
```

**Confidence:** Medium

---

### ECU-LIM-10: skipSafetyChecks is a Core-0-writable bool with no memory barrier; races with Core-1 safety check loop
- **Severity:** Medium
- **Bug class:** Concurrency / race condition
- **Location:** `src/engine/EngineData.h:117`, `src/engine/SafetyMonitor.h:55`

**Description:**
`ed.skipSafetyChecks` is a `volatile bool` written by Core 0 (via `CommandQueue` -> `handleCommand` -> `TOGGLE_SAFETY_CHECKS` at `main.cpp:1174`) and read by Core 1 at `SafetyMonitor.check()` line 55. On Xtensa ESP32, individual 32-bit-aligned reads/writes are single-cycle atomic (documented in EngineData.h:23-30), but `bool` is typically 8-bit and the Xtensa ISA does not guarantee atomicity for sub-word accesses. Additionally, there is no memory barrier between the command queue drain (which handles the toggle) and the subsequent `g_safety.check()` call. In practice, the bool toggle is benign, but the surrounding sequence (toggle + immediately enter engine-running state) is not atomic from Core 1's perspective.

More critically: `handleCommand` runs on Core 1 (called from `loop()` via `CommandQueue::drain()`), not Core 0. Core 0 only pushes the command; Core 1 processes it. So the toggle itself is single-core. The race is therefore less severe than it first appears -- but the `volatile` qualifier alone is insufficient to ensure compiler ordering for the multi-field read at check time (e.g., `mode` read then `skipSafetyChecks` read in separate loads).

**Trigger:**
Rapid toggle of skipSafetyChecks near a mode transition in DEV_MODE.

**Impact:**
SafetyMonitor might observe old `skipSafetyChecks=false` while also observing a new `mode=RUNNING` (or vice versa), performing one extra safety check cycle that was intended to be suppressed. In practice, one missed or extra cycle is unlikely to cause damage, but the gap in the bypass protocol is real.

**Evidence:**
- `EngineData.h:117`: `volatile bool skipSafetyChecks = false;`
- `SafetyMonitor.h:55`: `if (ed.skipSafetyChecks || ed.benchMode) return;`
- `main.cpp:1174`: toggle handled in `handleCommand` -- actually Core 1, so no true cross-core race, but no memory fence

**Suggested fix:**
Document that all `CommandQueue` handling is on Core 1 (clarifying no true cross-core race). For safety: add a comment in `SafetyMonitor.check()` noting that `skipSafetyChecks` is always written from Core 1 and the volatile read is sufficient.

**Confidence:** Medium (actual race is narrower than it appears; risk is documentation gap)

---

### ECU-LIM-11: Relight callback fires when _relightActive is already true -- igniterOn set redundantly but relightAttempts not incremented
- **Severity:** Medium
- **Bug class:** State machine / relight
- **Location:** `src/main.cpp:1578-1591`, `src/engine/SafetyMonitor.h:148-163`

**Description:**
The relight callback (set at line 1578) has this structure:
```c
if (!_relightActive) {
    ed.relightAttempts = ed.relightAttempts + 1;
    _relightActive = true;
    ...
}
// Keep igniter on — checkRelight() clears this when flame returns or N1 drops
ed.igniterOn = true;
```
`SafetyMonitor::check()` calls `_relight()` on every interval tick while `_relightStartMs != 0` and conditions are still met (line 151 re-calls `_relight()` on subsequent ticks; this is actually only called once -- `_relightStartMs == 0` on first call, set to `now`, then on the second call `_relightStartMs != 0` so the else branch checks timeout/viability, not `_relight()` again). Re-reading the code: `_relight()` is _only_ called when `_relightStartMs == 0` (line 149-152). On subsequent ticks, the else branch runs instead.

However, `checkRelight()` in main.cpp runs independently _every_ loop tick (line 1649) and also sets `ed.igniterOn = true` (line 504). So the igniter is kept on by two independent mechanisms. If `checkRelight()` clears `_relightActive` (because flame was detected or N1 dropped), but SafetyMonitor's `_relightStartMs` is still non-zero and the condition is still met (e.g. flame sensor glitch caused _relightActive to clear then re-trigger), `_relight()` will be called again with `_relightActive == false`, incrementing `relightAttempts` a second time for what is actually the same event. Counter will over-count.

**Trigger:**
Intermittent flame detection during relight (flame detected for one tick, then lost again), resetting `_relightActive` via `checkRelight()` while SafetyMonitor still has `_relightStartMs != 0`. Rare but plausible with noisy flame sensors.

**Impact:**
`relightAttempts` counter over-counts. If any logic gates on `relightAttempts` max count (currently only used for logging/display), that could suppress a valid relight attempt prematurely. Low operational impact currently, but the counter is used in `FlightRecorder::logRelight()`.

**Evidence:**
- `main.cpp:1580-1585`: `_relightActive` guards attempt increment
- `main.cpp:487-491`: `checkRelight()` clears `_relightActive` on flame detection
- `SafetyMonitor.h:170-171`: `_flameoutMs` and `_relightStartMs` reset on flame detection -- this would actually also reset the SafetyMonitor side correctly

On re-examination: when flame is detected, SafetyMonitor line 170-171 resets both timers. So the double-fire only happens if flame is detected by `checkRelight()` _before_ SafetyMonitor's next 100 ms tick processes the same flame reading. This is a one-tick window. Low probability.

**Suggested fix:**
When SafetyMonitor detects flameout recovery (line 170), also call into the relight reset path, or gate the `_relight()` callback on `!_relightActive`.

**Confidence:** Low

---

### ECU-LIM-12: Underspeed check fires for STARTUP if minRpm is misconfigured to 0 or near-0
- **Severity:** Medium
- **Bug class:** Input validation / fail-safe
- **Location:** `src/engine/SafetyMonitor.h:247-264`

**Description:**
`minRpm` is loaded from Config without a lower-bound check (Config.cpp:509: `minRpm = eng["min_rpm"] | minRpm`). If a user sets `min_rpm: 0` in config JSON, `minRpm = 0.0f`. In RUNNING mode, line 248:
```c
if (ed.n1Healthy && ed.n1Rpm < minRpm)  // 0 < 0 == false
```
The check never fires -- this is actually safe (an overly permissive threshold, not a false fault).

The `_startupSpooled` logic is more concerning: with `minRpm = 0`, `ed.n1Rpm >= minRpm` is immediately true at startup (line 258), setting `_startupSpooled = true` in the first tick of STARTUP even with the engine not moving. Then line 260 checks `_startupSpooled && ed.n1Rpm < minRpm` -- `0 < 0` is false, so no false fault. This path is also safe.

However, if `minRpm` is set to a very large value (e.g., equal to `rpmLimit`), then UNDERSPEED fires as soon as RUNNING is entered (engine at idle is always below rpmLimit). This is the more dangerous configuration: an overly high `minRpm` would trigger a false UNDERSPEED fault the moment the engine reaches RUNNING, even if N1 is perfectly healthy at idle.

**Trigger:**
Config JSON `min_rpm` set too high (e.g., accidentally entered in the same field as `rpm_limit`).

**Impact:**
False UNDERSPEED fault immediately on RUNNING entry. Engine performs unnecessary shutdown.

**Evidence:**
- `Config.cpp:509`: no validation on `minRpm` after load
- `SafetyMonitor.h:247-249`: UNDERSPEED fires immediately if `n1Rpm < minRpm` in RUNNING
- `Config.h:26-27`: `rpmLimit` and `minRpm` are adjacent float fields -- easy to confuse in JSON

**Suggested fix:**
Validate in `applyConfig()`:
```c
if (Config::minRpm >= Config::rpmLimit) {
    Serial.printf("[OT] WARNING: minRpm (%.0f) >= rpmLimit (%.0f) -- clamping\n", ...);
    g_safety.minRpm = Config::rpmLimit * 0.3f;  // safe fallback
}
```

**Confidence:** Medium

---

### ECU-LIM-13: _fallbackDesc stack buffer in SafetyMonitor::_trigger() is used after function scope via desc pointer
- **Severity:** Low
- **Bug class:** Memory safety / dangling pointer
- **Location:** `src/engine/SafetyMonitor.h:346-358`

**Description:**
In `_trigger()`, a stack-local buffer `_fallbackDesc[192]` is declared and used for unknown fault codes:
```c
char _fallbackDesc[192];
if (!desc) {
    snprintf(_fallbackDesc, sizeof(_fallbackDesc), ...);
    desc = _fallbackDesc;    // desc points to stack
}
// ...
strncpy(ed.faultDescription, desc, sizeof(ed.faultDescription) - 1);
```
The `strncpy` on the following line uses `desc` while `_fallbackDesc` is still in scope (inside the same function body). This is **safe in the current code** because `desc` is consumed before `_trigger()` returns.

However, `_lastFault = code` is set at the top of `_trigger()` (line 287), where `code` is the fault code string literal (e.g. `"HOT_START"`), not `_fallbackDesc`. So `lastFault()` does not point to the stack buffer. The `desc` pointer (which does point to the stack buffer) is only used locally and is never stored. No actual lifetime violation exists in the current code.

This is flagged as a hygiene issue: the pattern of `desc = _fallbackDesc` followed by use looks dangerous to reviewers and static analysers, and the variable name `_fallbackDesc` with a leading underscore implies class scope when it is actually local.

**Evidence:**
- `SafetyMonitor.h:346-358`: local buffer, local use, safe
- Pattern would be unsafe if `desc` were ever stored or returned

**Suggested fix:**
Use `strncpy` directly without the intermediate `desc` pointer, or move the buffer to class scope to make intent clear.

**Confidence:** High (it is safe; filing as Low/hygiene)

---

### ECU-LIM-14: Standby oil feed uses raw ed.n1Rpm without n1Healthy check -- stale-zero RPM prevents windmill protection
- **Severity:** Low
- **Bug class:** Fail-safe logic / sensor health
- **Location:** `src/main.cpp:511-539`

**Description:**
`checkStandbyOilFeed()` checks `hw.hasN1Rpm` (hardware presence) but does not check `ed.n1Healthy` before reading `ed.n1Rpm` (line 521):
```c
if (ed.n1Rpm >= Config::standbyOilRpmLimit) {
```
If the N1 sensor goes unhealthy (e.g., ZERO_STUCK fault during spindown into STANDBY), `n1Rpm` retains its last value or may be set to 0 by the sensor driver. If it is forced to 0, the standby oil feed will never activate even if the turbine is actually windmilling above `standbyOilRpmLimit`. The bearings will run dry.

**Trigger:**
N1 sensor failure or ZERO_STUCK during spindown; engine enters STANDBY while still windmilling.

**Impact:**
Oil pump not activated during windmill; bearing damage without lubrication.

**Evidence:**
- `main.cpp:513`: `hw.hasN1Rpm` checked, but `ed.n1Healthy` is not
- `main.cpp:521`: direct `ed.n1Rpm` comparison
- `EngineData.h:60`: `n1Healthy` is the health gate for all RPM-dependent decisions in SafetyMonitor

**Suggested fix:**
Either skip the oil feed check if sensor is unhealthy (safe default: activate feed anyway to protect bearings), or use a "last known good RPM" approach. The safer fail-safe is to activate standby oil feed when `n1Healthy` is false and the engine recently was in RUNNING:
```c
bool n1Valid = ed.n1Healthy && ed.n1Rpm > 0;
bool useRpm = n1Valid ? (ed.n1Rpm >= Config::standbyOilRpmLimit) : _recentlyRunning;
```

**Confidence:** Medium

---

## Notes / Unclear areas

1. **Config validation gap (all numeric limits):** None of `rpmLimit`, `totLimit`, `minRpm`, `titLimit`, `oilTempLimit`, `fuelPressMin`, or `battVoltMin` have lower-bound validation after JSON load. A zero or negative value in most cases silently disables the check (0 = disabled by design for optional fields), but for mandatory fields like `rpmLimit` and `totLimit`, a zero value means SafetyMonitor will fault immediately on first non-zero reading. A config schema validation pass at `Config::load()` time would eliminate this class of misconfiguration entirely.

2. **Surge buffer variance units:** `surgeRpmVariance` is documented as "RPM² variance threshold" (SafetyMonitor.h:33). At 60,000 RPM with a 1% surge oscillation (600 RPM amplitude), the variance would be approximately 360,000 RPM². Config.h:89 gives no default; Config.cpp shows `surgeDetectRpmVariance` defaults to 0 (disabled). The threshold must be tuned per engine, but there is no documented guidance or range check. An unreasonably small value (e.g. 1.0) would make surge detection fire on normal speed variation.

3. **Hot-start vs. overspeed precedence during STARTUP:** SafetyMonitor checks HOT_START before OVERSPEED (lines 77-89). If the engine is somehow running (hot from a previous start) and N1 exceeds rpmLimit, the HOT_START fault fires first, which is misleading but not unsafe -- both paths call `_trigger()` and initiate fault shutdown.

4. **relight callback interaction with enterShutdown:** If a separate fault (e.g. LOW_OIL) fires while a flameout relight is in progress, `enterFaultShutdown()` sets `mode = SHUTDOWN`, then `checkRelight()` on the very next tick detects `mode != RUNNING` and clears `_relightActive` and `igniterOn`. This sequencing is correct. However, between the `_trigger("LOW_OIL")` call and the next `checkRelight()` tick, `ed.igniterOn` remains true for one loop iteration. This is a one-tick window and the fuel solenoid will already be closing via the shutdown sequence, so the igniter spark alone cannot sustain combustion. Not filed as a bug but worth noting.

5. **checkIntervalMs drift:** The 100 ms check interval uses `_lastCheckMs = now` (SafetyMonitor.h:94) which resets to the start of the tick that passed the gate. At a 10 ms loop cycle, jitter of up to 10 ms per interval is expected. Over a 10-second window, cumulative drift can be up to 100 ms. This is acceptable for the protection functions described but means the "10 samples = ~1 second" comment on `SURGE_BUF` is approximate.
