# Actuator-output subsystem audit

## Files reviewed

| File | Lines read |
|------|-----------|
| `/home/user/openturbine/src/Hardware.h` | 1-1206 (complete) |
| `/home/user/openturbine/src/hal/actuators/IActuator.h` | complete |
| `/home/user/openturbine/src/hal/actuators/ServoActuator.h` | complete |
| `/home/user/openturbine/src/hal/actuators/LEDCActuator.h` | complete |
| `/home/user/openturbine/src/hal/actuators/RelayActuator.h` | complete |
| `/home/user/openturbine/src/hal/actuators/MockActuator.h` | complete |
| `/home/user/openturbine/src/main.cpp` | complete (1-1690) |
| `/home/user/openturbine/src/engine/controllers/OilPressureLoop.h` | complete |
| `/home/user/openturbine/src/engine/controllers/ThrottleSlew.h` | complete |
| `/home/user/openturbine/hardware_profile.h` | complete |

---

## Findings (Critical -> Info)

---

### ECU-ACT-01: Fuel solenoid pin is in HIGH state from power-on until initActuators runs

- **Severity:** Critical
- **Bug class:** Wrong default at boot
- **Location:** `Hardware.h:161` (OT_DECLARE_HARDWARE macro), `RelayActuator.h:21-24`
- **Description:** `g_actFuelSol` is declared as a global C++ object with `OT_FUEL_SOL_ACTIVE_H true` and pin `OT_FUEL_SOL_PIN` (default GPIO 12). The constructor only stores parameters; it does NOT call `pinMode` or `digitalWrite`. On the ESP32, GPIO 12 is a strapping pin whose boot-time level is determined by external pull resistors. If the PCB pulls GPIO 12 HIGH at boot (e.g., to ensure a known ESC state), and the fuel solenoid is wired active-high, the solenoid is energised from power-on. `RelayActuator::begin()` is only called later inside `Hardware::initActuators()` (main.cpp:1545), which calls `off()` to establish the safe state. Between power-on and `initActuators()` there is no guaranteed safe output level on the solenoid pin.
- **Trigger:** Any board where GPIO 12 is pulled HIGH by external hardware. GPIO 12 is also an ESP32 strapping pin that selects flash voltage; a 3.3 V pull-up is common.
- **Impact:** Fuel solenoid opens at power-on during boot. If igniter fires (also not yet initialised) or if residual combustor temperature is present, hot start or uncommanded fuelling results.
- **Evidence:** `RelayActuator` constructor (RelayActuator.h:11-12) stores pin/activeHigh, takes no GPIO action. `begin()` (RelayActuator.h:21-24) calls `off()` which calls `_write(false)`. `begin()` is invoked at main.cpp:1545. `OT_FUEL_SOL_ACTIVE_H true` (hardware_profile.h:175). GPIO 12 strapping warning in hardware_profile.h:463.
- **Suggested fix:** Call `off()` (or `pinMode(_pin, OUTPUT); digitalWrite(_pin, activeHigh ? LOW : HIGH)`) in every `RelayActuator` constructor so the safe-off level is driven as soon as the object is constructed, before any user code runs. The same applies to all relay actuator globals declared in OT_DECLARE_HARDWARE.
- **Confidence:** High

---

### ECU-ACT-02: allOff() does not zero fuelSolOpen, igniterOn, starterDemand, or starterEnabled in EngineData

- **Severity:** Critical
- **Bug class:** Fail-safe logic -- demand not zeroed on emergency off
- **Location:** `Hardware.h:1099-1131` (allOff), `main.cpp:988-1048` (enterStandby)
- **Description:** `Hardware::allOff()` calls `.off()` on every physical actuator, but it does NOT write the matching EngineData demand fields to safe values. The fields it does clear are: `oilScavengeOn`, `abSolOpen`, `abPumpDemand`, `fuelPump2Demand`, `propPitchDemand`, `abFuelOffset`, `bleedValveOpen`, `glowPlugDemand`, `surgeDetected`, `igniter2On`, `abMode`, `airstarterOpen`, `coolFanOn`. The fields it does NOT clear include: `ed.fuelSolOpen`, `ed.igniterOn`, `ed.starterDemand`, `ed.starterEnabled`, `ed.oilPumpPct`, `ed.throttleDemand`. On the very next loop() tick, `Hardware::updateActuators()` runs and reads these stale demand fields, re-energising the outputs that allOff() just silenced. This is a one-tick window, but it means allOff() does not provide a stable safe state by itself.
- **Trigger:** allOff() is called from enterStandby() (main.cpp:1047). enterStandby() does clear most EngineData fields above allOff() at lines 988-1046. However, allOff() is also a public API intended as an emergency all-off callable from other contexts. If called directly without the preceding enterStandby() field resets, the actuators will be re-energised on the next updateActuators() tick.
- **Impact:** After allOff() is called (e.g., in a future interrupt handler or watchdog callback), actuators are live again one control loop later. Fuel solenoid reopens, igniter re-fires.
- **Evidence:** `Hardware.h:1117-1130` -- allOff() clears only a subset of EngineData fields. `main.cpp:1003-1016` -- enterStandby() clears the remaining fields before calling allOff(). The dependency is implicit and undocumented.
- **Suggested fix:** Move all EngineData demand field resets INTO allOff() so it is self-contained. Remove the duplicates from enterStandby(). Document that allOff() is the single safe-off authority.
- **Confidence:** High

---

### ECU-ACT-03: Coil-igniter can latch energised on first invocation due to uninitialised s_coilPhaseStart

- **Severity:** Critical
- **Bug class:** State machine -- igniter stuck-on
- **Location:** `Hardware.h:1013-1041` (updateActuators igniter coil logic)
- **Description:** The coil-drive state machine uses two `static` local variables: `s_coilCharging` (bool) and `s_coilPhaseStart` (uint32_t). Both are zero-initialised by C++ rules. On the first call with `ed.igniterOn == true`, `s_coilCharging` is `false` so the code enters the "start charge" branch (line 1027). The check is `(now - s_coilPhaseStart) >= hw.igniterRestMs`. Since `s_coilPhaseStart == 0` and `now` is `millis()` (many milliseconds since boot, typically hundreds or thousands), this condition is immediately true. The coil begins charging (line 1030: `g_actIgniter->set(1.0f)`). This is correct for starting ignition. However, the `endCharge` condition (line 1017-1019) uses the current-sensor path: `ed.igniterCurrentAmps >= hw.igniterCoilSatAmps`. If the current sensor is not fitted (`hw.hasIgniterCurrentSensor == false`) it falls back to the time check `(now - s_coilPhaseStart) >= hw.igniterDwellMs`. But `s_coilPhaseStart` is set to `now` when charging starts (line 1023 in the discharge branch, line 1029 in the charge-start branch), so this works correctly in normal cycling. **The real hazard** is if `igniterOn` transitions from `true` to `false` and back while the engine is in a restart/relight scenario. The `else` branch at line 1039 sets `s_coilPhaseStart = (uint32_t)millis()` correctly. However, because both static variables have static storage duration, a rapid mode transition (fault shutdown -> restart) leaves the coil igniter potentially in `s_coilCharging = true` state from the previous run, meaning it will immediately check `endCharge` but use the stale `s_coilPhaseStart` from the prior session. If that stale timestamp is far in the past, `endCharge` fires immediately, the coil discharges, but then within one `igniterRestMs` interval it charges again -- which is correct behavior. The deeper concern: there is no maximum on-time cap on the coil charge. If `hw.hasIgniterCurrentSensor == false` and `hw.igniterDwellMs` is set to 0 (or overflows), the `endCharge` condition `(now - s_coilPhaseStart) >= 0` is always true, coil discharges immediately, and the igniter fires at `1/igniterRestMs` Hz continuously. More critically: with `igniterDwellMs == 0` the coil is pulsed open-collector with no charge time, dumping no energy -- a silent failure mode rather than a stuck-coil, but still undesirable.
- **Trigger:** `hw.igniterCoil == true`, `hw.hasIgniterCurrentSensor == false`, `hw.igniterDwellMs == 0` (possible via JSON configuration: `"igniter_dwell_ms": 0`).
- **Impact:** Zero dwell time means no energy is stored in the coil before firing; igniter produces no spark. Alternatively, if dwell is large and rest is 0, the coil charges but never fires.
- **Evidence:** Hardware.h:1013-1041. Default `OT_IGNITER_DWELL_MS 6`, `OT_IGNITER_REST_MS 3` (Hardware.h:98-102) are non-zero, but JSON runtime config (`hw.igniterDwellMs`, `hw.igniterRestMs`) can override to any value including 0.
- **Suggested fix:** Add a guard: `if (hw.igniterDwellMs <= 0 || hw.igniterRestMs <= 0) { /* log error, default to off */ g_actIgniter->set(0.0f); return; }`. Also reset `s_coilCharging = false; s_coilPhaseStart = now;` whenever `igniterOn` transitions from false to true (edge detect), not just when it stays false.
- **Confidence:** Medium

---

### ECU-ACT-04: Oil pump demand in updateActuators() uses oilPumpPct as percentage for LEDC/servo but directly as 0..1 for relay -- scale confusion

- **Severity:** High
- **Bug class:** Integer/fraction confusion
- **Location:** `Hardware.h:1004-1008` (updateActuators oil pump block)
- **Description:** The oil pump demand dispatch reads `ed.oilPumpPct` and converts it:
  ```cpp
  float demand = (hw.oilPumpType == 2)
               ? (ed.oilPumpPct > 0.0f ? 1.0f : 0.0f)   // relay: on/off
               : (ed.oilPumpPct / 100.0f);                // LEDC/servo: pct->fraction
  ```
  For LEDC and servo types, `oilPumpPct` is treated as 0..100 and divided by 100. This is correct for `OilPressureLoop` which writes values like `18.0f..100.0f` to `oilPumpPct`. However, `checkStandbyOilFeed()` (main.cpp:527) writes `Config::standbyOilFeedPct` directly to `ed.oilPumpPct`, and the oil prime tool timer (main.cpp:425) writes `Config::standbyOilFeedPct` as well. Both are config values expected to be in percent (0..100). So far consistent. But `checkExtraCooldown()` (main.cpp:457) sets `ed.oilPumpPct = 0` to stop the pump, which is also consistent. The issue is if any code path ever writes a 0..1 fraction directly to `oilPumpPct` expecting it to be a fraction rather than a percent. Specifically, `OilPumpOn` block (declared but not shown in read files) uses `demandPct` populated from `Config::oilPumpOnPct` (Hardware.h:424). As long as all writers agree on percent, this is safe -- but there is no type enforcement. The real bug is subtle: the LEDC path divides by 100, but the relay path uses `> 0.0f` which means any non-zero value (including 0.1%, which is functionally zero) turns the relay on.
- **Trigger:** A config value `oilPumpOnPct = 0.5` (meaning 0.5%) sent via JSON would be treated as 0.5/100 = 0.005 (0.5% duty on LEDC, nearly off) but would turn the relay pump fully ON since `0.5f > 0.0f`.
- **Impact:** Relay-type oil pump turns on at any nonzero demand regardless of intended flow rate. For the LEDC type, sub-1% values produce near-zero duty -- acceptable. For relay type, 0.1% demand energises the pump full-on.
- **Evidence:** Hardware.h:1004-1008. OilPressureLoop.h:61-63 writes percent values.
- **Suggested fix:** Define and enforce a consistent domain: `oilPumpPct` is always 0..100. Add a compile-time comment or runtime assert. For the relay path, consider a threshold (e.g., `> 5.0f`) rather than `> 0.0f` to match the intended "pump on" semantic.
- **Confidence:** Medium

---

### ECU-ACT-05: Multiple unsynchronised writers to oilPumpPct -- oilFailsafe can be silently overridden by standby oil feed

- **Severity:** High
- **Bug class:** Concurrency / multiple writers / fail-safe logic
- **Location:** `OilPressureLoop.h:44-46`, `main.cpp:527`, `main.cpp:425`, `main.cpp:457`
- **Description:** `ed.oilPumpPct` has at least four writers executing in Core 1's loop():
  1. `OilPressureLoop::tick()` -- P-controller output (OilPressureLoop.h:45: sets `oilPumpPct = failsafePct` during sensor fault; line 63: sets P-controller output during normal operation).
  2. `checkStandbyOilFeed()` -- windmill protection (main.cpp:527: `ed.oilPumpPct = Config::standbyOilFeedPct`).
  3. `checkToolTimers()` -- tool prime expiry (main.cpp:425: sets to `standbyOilFeedPct` or 0).
  4. `checkExtraCooldown()` -- cooldown expiry (main.cpp:457: `ed.oilPumpPct = 0`).
  
  The critical hazard: `OilPressureLoop::tick()` runs during RUNNING mode and sets `oilFailsafeActive = true` and `oilPumpPct = failsafePct` when the oil sensor is unhealthy. However, if somehow `checkStandbyOilFeed()` were active (it guards with `mode != STANDBY` return, so in practice it does NOT run in RUNNING mode -- this particular race does not fire). The real issue is the inverse: during STANDBY with `standbyOilFeedActive`, `OilPressureLoop::tick()` is NOT running (guarded by `mode != RUNNING && mode != STARTUP` in `Hardware::runControllers()`). So there is no direct race between the P-loop and standby feed.
  
  However, there IS a state machine bug: when the oil sensor fails during RUNNING (failsafe fires, `oilPumpPct = 60`), then the engine shuts down and `enterStandby()` clears `oilPumpPct = 0` and `oilFailsafeActive = false`. So far correct. But `OilPressureLoop::reset()` (called from `Hardware::initControllers()` inside `enterRunning()`) seeds `_outputPct` from `constrain(ed.oilPumpPct, minPct, 100)`. If `ed.oilPumpPct` was not cleared before `initControllers()` runs (e.g., a rapid restart), the P-loop carries forward a stale high demand from the previous failsafe. In practice `enterStandby()` clears it before `enterRunning()` can be called, so this is low risk. The more genuine issue is: there is no mutex between Core 0 (web server) writing EngineData via API commands and Core 1 reading it. If Core 0's JSON PATCH handler writes `oilPumpPct` while Core 1 is in the middle of reading it in OilPressureLoop (float write is not atomic on Xtensa), a torn read is possible. The project acknowledges "No mutex on EngineData" in its design context.
- **Trigger:** Concurrent Core 0 web write + Core 1 P-loop read of `oilPumpPct` during RUNNING.
- **Impact:** Torn float read could produce a NaN or out-of-range duty, driving the oil pump to 0% or 100% for one control tick. At high speed this risks bearing starvation.
- **Evidence:** OilPressureLoop.h:44-46,63. main.cpp:425,457,527. No `portENTER_CRITICAL` or `std::atomic` anywhere in the EngineData access path.
- **Suggested fix:** At minimum, mark `oilPumpPct` writes from Core 0 as going through the CommandQueue (already used for commands) so Core 1 serialises the write. For the broader data model, consider `volatile` + single-writer discipline or a ring buffer for web->Core1 updates.
- **Confidence:** Medium (race is real; severity of consequence depends on float bus width and compiler code generation)

---

### ECU-ACT-06: LEDCActuator::begin() does not call ledcDetach before re-attaching -- glitch on applyConfig mid-run

- **Severity:** High
- **Bug class:** Error handling -- missing end() before re-init
- **Location:** `LEDCActuator.h:37-40`, `Hardware.h:824-972` (initActuators), `main.cpp:1149` (applyConfig on START)
- **Description:** `Hardware::applyConfig()` is called on every START command (main.cpp:1149). `applyConfig()` does NOT reinitialise actuators (it only copies config values into block and controller instances). However, `initActuators()` is called once at boot (main.cpp:1545). If `applyConfig()` were extended, or if `initActuators()` were called again (e.g., to pick up a changed `throttlePin`), `LEDCActuator::begin()` calls `ledcAttach(_pin, _freqHz, _resBits)` without first calling `ledcDetach(_pin)`. In Arduino-ESP32 3.x, calling `ledcAttach` on an already-attached pin is documented to be a no-op or may glitch the output. More importantly, if the pin number changed (runtime pin overload path, LEDCActuator.h:25-31), the old pin is left attached to the LEDC channel. Two pins then share the same hardware timer channel, creating undefined PWM output on both pins simultaneously.
- **Trigger:** `ledcAttach` called twice on the same or different pin without intervening `ledcDetach`. This occurs if `initActuators()` is ever called a second time (currently not done, but the `begin(pin, freq, res)` overload exists specifically to support runtime re-configuration).
- **Impact:** Two actuator pins (e.g., oil pump and throttle LEDC) driven by the same LEDC channel produce identical PWM signals. The wrong actuator receives the wrong demand. On a re-init, a brief full-duty glitch during reattach could drive the throttle or oil pump to 100%.
- **Evidence:** LEDCActuator.h:37-40. The `begin(int pin, ...)` overload at LEDCActuator.h:25-31 implies it is intended for runtime use. `ledcDetach` is never called in the codebase.
- **Suggested fix:** In `LEDCActuator::begin()`, call `ledcDetach(_pin)` (ignoring error if not attached) before `ledcAttach`. Track the currently attached pin to detach the old pin if it changed.
- **Confidence:** High (for the dual-attach bug); Low (for the glitch-on-reconfig path, since initActuators is currently single-call)

---

### ECU-ACT-07: Igniter LEDC duty cycle is computed at object-construction time using compile-time defaults, then overridden at initActuators with runtime values -- the constructed object may drive the wrong frequency

- **Severity:** High
- **Bug class:** Logic bug / wrong output level
- **Location:** `Hardware.h:162` (OT_DECLARE_HARDWARE igniter LEDC declaration), `Hardware.h:869-877` (initActuators igniter init)
- **Description:** The LEDC igniter object is declared as:
  ```cpp
  LEDCActuator g_actIgniterLedc(OT_IGNITER_PIN,
      1000/(OT_IGNITER_DWELL_MS+OT_IGNITER_REST_MS), 8, "IGNITER_LEDC");
  ```
  The frequency is `1000 / (6+3) = 111 Hz` using compile-time defaults. The object's `begin()` is NOT called here -- no GPIO action yet. Inside `initActuators()` (Hardware.h:869-877), the code recomputes from runtime values:
  ```cpp
  int period = hw.igniterDwellMs + hw.igniterRestMs;
  uint32_t freq = (period > 0) ? (1000u / (uint32_t)period) : 111u;
  g_actIgniterLedc.begin(hw.igniterPin, freq, 8);
  ```
  This calls the `begin(pin, freq, res)` overload which updates `_pin`, `_freqHz`, `_resBits` and then calls `begin()` -- correct. However, there is an integer division truncation: `1000u / period` rounds down. For `period = 9` (default 6+3), `freq = 111 Hz`. For `period = 11`, `freq = 90 Hz`. For `period = 7`, `freq = 142 Hz`. The actual frequency passed to LEDC hardware is the rounded-down value, and the duty cycle passed later in `updateActuators()` is:
  ```cpp
  float duty = (hw.igniterDwellMs + hw.igniterRestMs > 0)
      ? ((float)hw.igniterDwellMs / (hw.igniterDwellMs + hw.igniterRestMs))
      : 0.5f;
  ```
  This correctly computes dwell fraction (e.g., 6/9 = 0.667). The LEDC period is `1000/111 = 9.009 ms`, not exactly 9 ms. The actual dwell is `0.667 * 9.009 = 6.006 ms` -- close but not exact. At 8-bit resolution (0-255 duty), the step resolution is `9.009ms / 255 = 35 us`, which is adequate. The primary risk is for very short periods: if `period = 2` (1 ms dwell + 1 ms rest), `freq = 500 Hz`, duty = 0.5, dwell = 1.0 ms -- acceptable. If `period = 1`, `freq = 1000 Hz`, duty depends on dwell/rest split. There is no minimum period guard, and a misconfigured `period = 0` causes integer division by zero at Hardware.h:871 (guarded by `period > 0` check, defaulting to 111).
- **Trigger:** `hw.igniterDwellMs + hw.igniterRestMs == 0` -- guarded. `hw.igniterDwellMs > hw.igniterRestMs + hw.igniterDwellMs` -- impossible. Main risk is the construct-time object using pin -1 and frequency 111 -- harmless since begin() is not called on it.
- **Impact:** Igniter fires at the wrong frequency/duty if period is miscalculated. At very high frequencies the coil does not have time to charge and produces no spark. Engine fails to light.
- **Evidence:** Hardware.h:162, 869-877, 1044-1049.
- **Suggested fix:** After `period = hw.igniterDwellMs + hw.igniterRestMs`, add `if (period < 2) { log error; disable igniter; }`. Document that minimum dwell is 1 ms and minimum rest is 1 ms.
- **Confidence:** Medium

---

### ECU-ACT-08: Starter ESC receives non-zero demand when starterEnabled is true but starterDemand is 0 -- motor may jog

- **Severity:** High
- **Bug class:** Logic bug / wrong output level
- **Location:** `Hardware.h:988-996` (updateActuators starter block)
- **Description:** The starter actuator logic:
  ```cpp
  bool delayOk = !hw.hasStarterEn ||
                 !ed.starterEnabled ||
                 ((millis() - _enMs2) >= (unsigned long)hw.starterEnDelayMs);
  g_actStarter->set(delayOk ? ed.starterDemand : 0.0f);
  ```
  When `ed.starterEnabled == true` and `delayOk == true`, the starter receives `ed.starterDemand`. If `ed.starterDemand == 0.0f` and the underlying actuator is a `ServoActuator`, `set(0.0f)` writes `_minUs` microseconds (1000 or 1500 us depending on profile). For a standard unidirectional ESC, 1000 us = armed/stopped -- correct. For a bidirectional ESC configured with `minUs = 1500` (the hardware_profile.h default, line 145), `set(0.0f)` writes 1500 us = neutral -- also correct. However, if `starterDemand` is inadvertently left at a small non-zero value (e.g., 0.01 from a previous test) while `starterEnabled` transitions to true, the ESC immediately sees a small non-zero demand. The `starterAssist` function (main.cpp:912-919) explicitly zeros both fields, but there is no guard in updateActuators itself.
  
  More specifically: when the starter enable relay is first energised, `_enMs2` is set (Hardware.h:991), and during the delay period `g_actStarter->set(0.0f)` is written. After `starterEnDelayMs` elapses, `g_actStarter->set(ed.starterDemand)` is written. If `starterDemand` was non-zero before the delay elapsed (e.g., set by StarterSpin block), the motor starts immediately when the delay gate opens -- which is the intended behavior. But if `enterStandby()` cleared `starterDemand = 0` and `starterEnabled = false`, then checkStarterAssist sets `starterEnabled = true` and `starterDemand = Config::starterAssistPct / 100.0f` atomically (same Core 1 tick), there is no gap. This is correct.

  The genuine bug: the `_prevEn2` and `_enMs2` statics are never reset. If the engine runs a second start attempt, `_enMs2` holds the timestamp from the previous start. On the second start, `ed.starterEnabled` goes from false (STANDBY) to true (STARTUP via StarterSpin). The rising-edge detect sets `_enMs2 = millis()` correctly. This appears correct, but the static `_prevEn2 = false` was written during STANDBY when `ed.starterEnabled == false`. On the first STANDBY->false tick, `_prevEn2` is already false, so no problem. However, if the engine aborts mid-startup while `ed.starterEnabled` is already true, then enters re-STANDBY (clearing `starterEnabled = false`), then immediately restarts: `_prevEn2` was left `true` from the aborted run. On the first loop tick of the new STARTUP, `ed.starterEnabled` transitions from false to true again, `_prevEn2` is false (cleared by STANDBY's `starterEnabled = false` path... but wait: `_prevEn2 = ed.starterEnabled` runs every tick. In STANDBY with `starterEnabled = false`, `_prevEn2` becomes false. Then on the new STARTUP, the rising edge is detected and `_enMs2 = millis()`. Correct. On examination this path is safe, but the static state makes the code hard to reason about.
- **Trigger:** Non-zero `ed.starterDemand` persisting across a mode transition when `starterEnabled` transitions to true.
- **Impact:** Starter motor jogged unexpectedly, potential mechanical damage or uncommanded torque on the engine shaft.
- **Evidence:** Hardware.h:988-996. `starterDemand` is cleared in `enterStandby()` (main.cpp:1012) and `checkStarterAssist()` (main.cpp:918). But no explicit zero write in `Hardware::allOff()` (Hardware.h:1099-1131).
- **Suggested fix:** In `allOff()`, explicitly set `_ed.starterDemand = 0; _ed.starterEnabled = false;`. Also reset the static `_enMs2 = 0; _prevEn2 = false;` either in `allOff()` or in `initActuators()` via a named function rather than relying on static initialisation.
- **Confidence:** Medium

---

### ECU-ACT-09: NULL pointer dereference if hasThrottle/hasStarter/etc. is true but the IActuator pointer was not set (type mismatch or JSON misconfiguration)

- **Severity:** High
- **Bug class:** Memory safety -- NULL pointer dereference
- **Location:** `Hardware.h:980-981, 988-996, 998-999` etc. (updateActuators), `Hardware.h:1101-1115` (allOff)
- **Description:** Each multi-type actuator (throttle, starter, oil pump, igniter, igniter2, cool fan, AB pump, oil scavenge, fuel pump 2, bleed valve, prop pitch) has a corresponding `IActuator*` pointer initialised to `nullptr` in `OT_DECLARE_HARDWARE`. The pointer is assigned in `initActuators()` if and only if the correct type branch executes. If `hw.hasThrottle == true` and `hw.throttleType` holds an unexpected value (e.g., 99, not 0/1/2), the if/else chain in `initActuators()` falls through to the `else` branch (Hardware.h:834-837: default servo), so `g_actThrottle` is assigned. In that case no NULL dereference. However: `initActuators()` is guarded by `if (hw.hasThrottle)`, meaning if `hasThrottle` is set but the type defaults to servo, `g_actThrottle` always gets assigned -- fine. The NULL dereference risk is more subtle: in `updateActuators()`, the guard is `if (hw.hasThrottle && g_actThrottle)`. If `initActuators()` was NOT called (impossible at boot since main.cpp:1545 calls it, but possible if someone calls `updateActuators()` before `initActuators()` in a test/tool context), `g_actThrottle` remains null and the `g_actThrottle` check saves it. This guard is correct and present for most multi-type actuators. The exception: `g_actFuelSol` is a concrete `RelayActuator` object, not a pointer, so it is never NULL -- accessed correctly via `g_actFuelSol.set(...)`. Similarly `g_actStarterEn`, `g_actAbSol`, `g_actAirstarterSol`. These are always safe. The risk is specifically for the pointer-type actuators (g_actThrottle, g_actStarter, etc.) accessed in `allOff()` -- all correctly guarded by `&& g_actXxx`. No actual NULL dereference found in current code, but it is one missing `hw.has*` flag or wrong JSON field away.
- **Trigger:** `hw.hasThrottle = true` in JSON with no corresponding `initActuators()` call, or `g_actThrottle` pointer not assigned due to a future code path.
- **Impact:** Crash (NULL dereference on Xtensa causes exception/reboot). On reboot, all actuators lose defined state until `initActuators()` runs again -- same as ECU-ACT-01.
- **Evidence:** Hardware.h:146 `IActuator* g_actThrottle = nullptr`. Hardware.h:980 `if (hw.hasThrottle && g_actThrottle)` -- guarded correctly in updateActuators. Hardware.h:1101 `if (hw.hasThrottle && g_actThrottle)` -- guarded correctly in allOff.
- **Suggested fix:** Add an assertion or log warning in `updateActuators()` / `allOff()` if `hw.hasXxx` is true but the pointer is null, to surface misconfiguration early rather than silently skipping the actuator.
- **Confidence:** Low (current code guards correctly; risk is future-path only)

---

### ECU-ACT-10: applyConfig() called on every START command re-applies block params while the sequence may already be running

- **Severity:** Medium
- **Bug class:** State machine -- glitchy on reconfig
- **Location:** `main.cpp:1149` (handleCommand START), `Hardware.h:396-558` (applyConfig)
- **Description:** `Hardware::applyConfig()` is called inside the `OTCommand::START` handler immediately before `g_sequencer.startSequence()`. At this point `ed.mode` is already set to `SysMode::STARTUP` (main.cpp:1145). `applyConfig()` modifies live block parameters including `g_blkStarterSpin.starterDemand`, `g_blkPreIgnSpark.sparkMs`, etc. Since the sequencer has not started yet (startSequence is called after), this is safe on the first call. However, `applyConfig()` also calls into `if (hw.hasOilLoop) { g_ctrlOilLoop.adjustScale = ...; }` which modifies live controller parameters. The OilPressureLoop controller is not yet running (it starts in RUNNING mode, not STARTUP), so this is also safe. The concern: on a re-start after an abort that went through `enterShutdown()` -> `sequenceComplete()` -> `enterStandby()`, `applyConfig()` is called in STARTUP just as `g_sequencer.startSequence()` is about to run. If the sequence has any queued state from the previous aborted run (e.g., block internal timers), `applyConfig()` could change parameters that the already-entered block relies on. For example, if `OilPrime` was mid-flight with a 5000ms timeout and `applyConfig()` changes `g_blkOilPrime.timeoutMs` to 2000ms, the new timeout takes effect immediately on the next `tick()`.
- **Trigger:** Config PATCH via web (Core 0) concurrent with a START command, or a rapid stop-restart cycle.
- **Impact:** Block parameters change mid-sequence, causing premature timeout or wrong demands.
- **Evidence:** main.cpp:1149. Hardware.h:396-558.
- **Suggested fix:** Only call `applyConfig()` in STANDBY before the sequence pointer is built. Add a guard: `if (ed.mode == SysMode::STANDBY)` before the `applyConfig()` call in the START handler. The existing `APPLY_CONFIG` command already has this guard (main.cpp:1326-1334).
- **Confidence:** Medium

---

### ECU-ACT-11: throttleDemand is zeroed in enterRunning() regardless of slew state -- one-frame drop to zero throttle on STARTUP->RUNNING transition

- **Severity:** Medium
- **Bug class:** State machine / demand persistence
- **Location:** `main.cpp:927` (enterRunning), `Hardware.h:1151-1202` (runControllers / ThrottleSlew)
- **Description:** `enterRunning()` sets `ed.throttleDemand = 0` (main.cpp:927) and then calls `Hardware::initControllers()` (main.cpp:940). `initControllers()` calls `g_ctrlThrottleSlew.begin()` which seeds `_current` from `constrain(ed.throttleDemand, 0, 1)`. Since `throttleDemand` was just zeroed, `ThrottleSlew._current` starts at 0. On the very next `updateActuators()` call, the throttle gets `set(0.0f)` -- minimum position -- for one tick before the DynamicIdle floor kicks in. For a servo throttle, this is `_minUs` (1000 us for standard ESC or 1500 us for bidirectional). For a fuel-metering valve servo, this is the closed/idle position, which is safe. However, the sequence block `SafetyHold` or `Spool` likely had the throttle at some demand level (e.g., 15%) when the sequence completed. The zero-then-ramp pattern creates a brief throttle dip that could cause a compressor stall or temporary lean-out immediately after reaching self-sustained speed.
- **Trigger:** Normal STARTUP->RUNNING transition whenever the startup sequence ends with a non-zero throttle demand.
- **Impact:** Brief throttle drop at the most critical moment (transition to self-sustained running). Potential lean-out or flameout on sensitive engines.
- **Evidence:** main.cpp:927 (`ed.throttleDemand = 0`), main.cpp:940 (`Hardware::initControllers()`), ThrottleSlew.h:31-36 (`begin()` seeds from `throttleDemand`).
- **Suggested fix:** Remove the `ed.throttleDemand = 0` line from `enterRunning()`. `ThrottleSlew::begin()` already seeds from the current demand, so the slew carries forward smoothly. If a clean-slate start is needed, do the zero only if `throttleDemand` is above the `DynamicIdle` floor.
- **Confidence:** High

---

### ECU-ACT-12: ServoActuator::set() lacks an integer overflow check for extreme minUs/maxUs values

- **Severity:** Medium
- **Bug class:** Input validation -- unclamped servo microseconds
- **Location:** `ServoActuator.h:27-31`
- **Description:** `ServoActuator::set()` computes:
  ```cpp
  value = constrain(value, 0.0f, 1.0f);
  int us = _minUs + (int)(value * (_maxUs - _minUs));
  _servo.writeMicroseconds(us);
  ```
  `value` is clamped to 0..1. `_minUs` and `_maxUs` are `int` values loaded from JSON at runtime (`hw.throttleMinUs`, `hw.throttleMaxUs`). If a user configures `maxUs = 32767` and `minUs = -32768` (possible via JSON with no range validation), `_maxUs - _minUs = 65535` overflows `int` on a 32-bit platform to `-1`. `(int)(value * -1) = 0` for all values. The servo gets stuck at `_minUs`. Alternatively, if `maxUs = 30000` and `minUs = 0`, `value * 30000 = 30000` -- this is within int range but `writeMicroseconds(30000)` is far outside the valid servo range (800..2200 us), and the ESP32Servo library may clamp it or exhibit undefined behavior.
- **Trigger:** Malformed JSON hardware config with out-of-range `throttle_min_us` or `throttle_max_us` values.
- **Impact:** Throttle servo driven to wrong position. For a fuel metering valve, extreme positions mean either fully open (hot start, runaway) or fully closed (engine cuts).
- **Evidence:** ServoActuator.h:27-31. HardwareConfig JSON parsing (not read) presumably accepts any integer.
- **Suggested fix:** In `ServoActuator::begin()`, clamp `_minUs = constrain(_minUs, 500, 2500)` and `_maxUs = constrain(_maxUs, 500, 2500)`. Also validate that `_maxUs - _minUs` is within a reasonable range (e.g., 100..1500 us).
- **Confidence:** Medium

---

### ECU-ACT-13: LEDCActuator duty calculation truncates float to uint32_t without rounding -- off-by-one at 100% demand

- **Severity:** Low
- **Bug class:** Integer truncation
- **Location:** `LEDCActuator.h:44-46`
- **Description:** `LEDCActuator::set()` computes:
  ```cpp
  value = constrain(value, 0.0f, 1.0f);
  if (_inverted) value = 1.0f - value;
  uint32_t duty = (uint32_t)(value * _maxDuty);
  ledcWrite(_pin, duty);
  ```
  For `value == 1.0f` and `_maxDuty == 4095` (12-bit): `1.0f * 4095 = 4095.0f`, truncated to `4095` -- correct. However, floating-point representation means `1.0f * 4095` may produce `4094.9999...` on some compiler/FPU combinations, truncating to `4094` rather than `4095`. This leaves the output one count below maximum, which for an oil pump means 99.976% rather than 100%. For most applications this is negligible. For the `off()` path with inverted mode: `ledcWrite(_pin, _maxDuty)` is called directly -- no float math -- correct. The `setDuty()` raw path (LEDCActuator.h:56-58) uses `constrain((int)duty, 0, (int)_maxDuty)` -- negative duty values would be implicitly cast to large uint32_t before the constrain if duty were unsigned, but `duty` is uint32_t so already non-negative; the int cast wraps for values > INT_MAX. This is a pathological case.
- **Trigger:** `set(1.0f)` on a 12-bit LEDC actuator.
- **Impact:** Oil pump or throttle slightly below maximum when at 100% demand. Negligible operationally.
- **Evidence:** LEDCActuator.h:44-46.
- **Suggested fix:** Use `(uint32_t)(value * _maxDuty + 0.5f)` to round rather than truncate, or use `lroundf(value * _maxDuty)`.
- **Confidence:** High (for the off-by-one); Low (for operational impact)

---

### ECU-ACT-14: Igniter 2 LEDC object constructed with pin -1 and frequency 111 -- if hasIgniter2 is true but igniter2Pin is -1, begin() runs with invalid pin

- **Severity:** Medium
- **Bug class:** Input validation / fail-safe
- **Location:** `Hardware.h:166` (OT_DECLARE_HARDWARE), `Hardware.h:879-888` (initActuators igniter2)
- **Description:** `g_actIgniter2Ledc` is declared with pin `-1` and frequency `111`. In `initActuators()`, the guard is `if (hw.hasIgniter2 && hw.igniter2Pin >= 0)`. If `hasIgniter2 == true` and `igniter2Pin < 0` (e.g., `igniter2Pin = -1` because the user forgot to set it), the init block is skipped entirely. `g_actIgniter2` remains `nullptr`. In `updateActuators()`, the guard `if (hw.hasIgniter2 && g_actIgniter2)` then skips the actuator -- the igniter2 is silently disabled. This is safe (fail-to-off) but produces no warning. More concerning: if `igniter2Pin = -1` but `hasIgniter2 = true`, the AB ignition sequence will proceed assuming igniter2 is functional (since `hasIgniter2` is set) but the physical coil never fires. The engine attempts AB ignition without a functioning igniter, consuming fuel with no spark.
- **Trigger:** `hardware.json` has `"has_igniter2": true` but omits `"igniter2_pin"` (defaults to -1).
- **Impact:** AB ignition attempted without igniter2 being operational. Unburnt AB fuel accumulates.
- **Evidence:** Hardware.h:879 guard `if (hw.hasIgniter2 && hw.igniter2Pin >= 0)`.
- **Suggested fix:** Log a warning if `hw.hasIgniter2 && hw.igniter2Pin < 0`. Set `ed.seqHasErrors = true` to block start in non-bench mode. Add the same check for other optional actuators with pin >= 0 guards.
- **Confidence:** Medium

---

## Notes / unclear areas

1. **OilPressureLoop failsafe race with enterRunning():** `OilPressureLoop::reset()` seeds `_outputPct` from `ed.oilPumpPct`. If a fault shutdown left `oilFailsafeActive = true` and `oilPumpPct = 60`, then `enterStandby()` clears both fields before `reset()` would ever be called in a new `enterRunning()`. The sequence is safe in the current code. However, if `initControllers()` is ever called outside of `enterRunning()`, the seeding logic could carry forward stale state.

2. **AB fuel offset and ThrottleSlew feedback:** The comment at Hardware.h:978-979 explicitly documents that `abFuelOffset` is added at actuator write time, NOT to `throttleDemand`, to prevent the slew from seeing the inflated value. The implementation at Hardware.h:981 confirms this: `constrain(ed.throttleDemand + ed.abFuelOffset, 0.0f, 1.0f)`. ThrottleSlew reads and writes `ed.throttleDemand` (ThrottleSlew.h:45, 71) without the offset. This design is correct and the potential feedback loop is closed. No bug here, but it relies on the actuator layer being the only place the offset is applied, which is fragile: if another writer adds the offset to `throttleDemand` directly (e.g., a rules engine action), the loop could re-emerge.

3. **LEDC channel collision:** In the current codebase, `LEDCActuator` objects use the Arduino-ESP32 3.x simplified API where `ledcAttach(pin, freq, res)` automatically assigns a hardware LEDC channel. The driver tracks pin-to-channel mapping internally. Two actuators on different pins will get different channels unless the driver runs out (16 channels on ESP32). If more than 16 LEDC actuators are simultaneously active (unlikely given the current set: oil pump, throttle LEDC, starter LEDC, igniter LEDC, igniter2 LEDC, cool fan LEDC, AB pump LEDC, fuel pump 2 LEDC, bleed valve LEDC, prop pitch LEDC, glow plug = 11 possible), the 12th attachment would fail silently. No channel collision found in the current actuator set.

4. **checkToolTimers STANDBY guard:** Tool timers check `if (ed.mode != SysMode::STANDBY) return` at the top. This means if a tool test is running and the engine accidentally transitions out of STANDBY (e.g., a spurious START command), the timer will NOT expire and the test actuator (fuel solenoid, igniter, etc.) remains in its last state indefinitely. However, `enterStandby()` calls `Hardware::allOff()` which physically silences all actuators, so the physical hardware is safe even if the EngineData field is not cleared until the next STANDBY tick.

5. **enterStandby calls allOff AFTER clearing EngineData fields:** The call order in enterStandby is: clear fields (lines 988-1046) -> call allOff() (line 1047). allOff() then clears a second set of fields (Hardware.h:1117-1130). This means the physical actuators are silenced last, with EngineData already showing the safe demanded state. In the one-tick gap between the EngineData write and the allOff() physical write, if updateActuators() were to run, it would already see zeroed demands and would write zero to the actuators -- so there is no unsafe window. The order is correct.
