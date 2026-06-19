# Sensor-input subsystem audit

## Files reviewed
- `src/hal/sensors/ISensor.h`
- `src/hal/sensors/PCNTRpmSensor.h`
- `src/hal/sensors/MAX6675TempSensor.h`
- `src/hal/sensors/MAX31855TempSensor.h`
- `src/hal/sensors/MAX31856TempSensor.h`
- `src/hal/sensors/DS18B20TempSensor.h`
- `src/hal/sensors/NTCSensor.h`
- `src/hal/sensors/AnalogSensor.h`
- `src/hal/sensors/MockSensor.h`
- `src/hal/RCInput.h`
- `src/Hardware.h` (initSensors lines 561-690, updateSensors lines 693-821)
- `src/engine/EngineData.h`
- `src/engine/SafetyMonitor.h`
- `src/system/Config.cpp`, `src/system/HardwareConfig.cpp`

---

## Findings (Critical -> Info)

### ECU-SEN-01: Flame sensor always reports healthy regardless of ADC state
- **Severity:** Critical
- **Bug class:** Fail-safe logic / Error handling
- **Location:** `src/hal/sensors/AnalogSensor.h:143` (`AnalogThSensor::isHealthy`)
- **Description:** `AnalogThSensor::isHealthy()` is hardcoded to `return true`. The flame sensor has no ADC rail check. A broken wire (ADC = 0, below threshold), a shorted ADC pin (ADC = 4095, above threshold), or a blinding ambient-light event can lock the output at 0.0 or 1.0 with no health flag ever set.
- **Trigger:** Open-circuit wire (ADC stuck at 0) -> `getValue()` returns 0.0 -> `flameDetected = false` always. During RUNNING mode with `flameMonitorActive`, the flameout timer fires after `flameoutShutdownMs` milliseconds, triggering a false FLAMEOUT abort on a healthy running engine. Alternatively, ADC stuck at 4095 -> `flameDetected = true` always -> relight mechanism never triggers on a real flameout.
- **Impact:** False FLAMEOUT fault aborts a healthy engine repeatedly after a single wiring fault. Or genuine flameout is silently ignored and relight is never attempted.
- **Evidence:** `src/hal/sensors/AnalogSensor.h:143`: `bool isHealthy() override { return true; }`. `src/engine/SafetyMonitor.h:141-159`: flameout path consults only `ed.flameDetected`, not any health flag.
- **Suggested fix:** Implement `isHealthy()` in `AnalogThSensor` as `return _railCheck()` (same pattern as `AnalogPolySensor`). Additionally add a separate `_flameHealthy` flag propagated to `ed.flameHealthy` in `updateSensors`, and gate the flameout monitor on `ed.flameHealthy`.
- **Confidence:** High

---

### ECU-SEN-02: PCNTRpmSensor SATURATED false-positive during normal deceleration
- **Severity:** High
- **Bug class:** Logic bug / Fail-safe logic
- **Location:** `src/hal/sensors/PCNTRpmSensor.h:133-135` (`_updateHealth`)
- **Description:** The SATURATED fault fires when `_prevRpm > 500.0f && delta == 0`. A delta of zero is perfectly normal when the engine decelerates rapidly through the 500 RPM band: in one 100 ms tick the shaft could have gone from ~550 RPM to rest, producing zero new pulses. The check cannot distinguish a stopped engine from a frozen sensor.
- **Trigger:** Engine coasts down from idle; one tick has `_prevRpm` slightly above 500 and no new pulses counted. `SATURATED` is set -> `_health.any()` is true -> `isHealthy()` returns false -> `ed.n1Healthy = false` in `updateSensors` (line 699) -> `SafetyMonitor` sets `limpMode = true` (line 253) for an engine that is actually stopping normally.
- **Impact:** Normal shutdown or rapid deceleration from low idle can trip `limpMode` unexpectedly, potentially masking subsequent real faults. False SATURATED can also interact with underspeed checks.
- **Evidence:** `src/hal/sensors/PCNTRpmSensor.h:133`: `if (_prevRpm > 500.0f && delta == 0)`. `src/Hardware.h:699`: `ed.n1Healthy = g_sensorN1Rpm.isHealthy()`. `src/engine/SafetyMonitor.h:252-254`.
- **Suggested fix:** Only raise SATURATED if the delta is zero AND `_prevRpm` is well above idle (e.g., `> 2000.0f`) AND the condition persists for at least two consecutive ticks, similar to the ZERO_STUCK counter pattern. Alternatively, suppress SATURATED when the mode is SHUTDOWN.
- **Confidence:** High

---

### ECU-SEN-03: ISR pulse-width acceptance window is hardcoded and does not match configurable rcMinUs/rcMaxUs
- **Severity:** High
- **Bug class:** Input validation / Logic bug
- **Location:** `src/hal/RCInput.h:92`, `src/hal/RCInput.h:101`
- **Description:** Both ISRs accept pulses only in the range 800-2200 us (hardcoded). The normalization in `_updateCh` (line 115) maps pulses to 0..1 using `Config::rcMinUs` and `Config::rcMaxUs`, which are independently configurable. If the operator sets `rcMinUs` below 800 or `rcMaxUs` above 2200, pulses at the extremes of the configured range are silently discarded by the ISR. The channel reports `fresh = false`, so `_updateCh` eventually lets the failsafe timer expire -> `valid = false` -> throttle demand drops to 0.0 at the worst moment (full throttle command).
- **Trigger:** Operator sets `rcMinUs = 900` and `rcMaxUs = 2100` with a transmitter that outputs 910 us at minimum stick. Pulses between 910 and 1000 us (or at the high end) are accepted. But if a transmitter outputs 950 us at low trim and `rcMinUs = 950`, the ISR accepts it. If `rcMinUs = 750` or `rcMaxUs = 2300`, those extreme positions are dropped every time.
- **Impact:** False failsafe at commanded throttle extremes; in the worst case this causes unexpected power reduction mid-flight or mid-sequence.
- **Evidence:** `src/hal/RCInput.h:92`: `if (pw >= 800 && pw <= 2200)`. `src/hal/RCInput.h:113-115`: uses `Config::rcMinUs` and `Config::rcMaxUs` for normalization. `src/system/Config.cpp:709-710`: these values are runtime-configurable from JSON.
- **Suggested fix:** Replace the hardcoded 800/2200 guards in the ISRs with `Config::rcMinUs - 200` and `Config::rcMaxUs + 200` (a margin wider than the calibration range but still rejecting genuine glitches), or use a fixed sane servo range (e.g., 500-2500 us) and let `_updateCh` clamp via `constrain`.
- **Confidence:** High

---

### ECU-SEN-04: PCNTRpmSensor does not guard against _ppr == 0
- **Severity:** High
- **Bug class:** Integer issue / Input validation
- **Location:** `src/hal/sensors/PCNTRpmSensor.h:85`, `src/hal/sensors/PCNTRpmSensor.h:23-27`
- **Description:** `update()` computes `newRpm = ((float)delta / _ppr) * (60000.0f / (float)dt)`. If `_ppr` is 0.0 (e.g., user sends `"ppr": 0` in the JSON config and `HardwareConfig::loadFromJson` applies it via `n1RpmPpr = n1["ppr"] | n1RpmPpr`), the division produces `+Inf` or `NaN`. The JUMP health check (`fabsf(inf - _prevRpm) > maxDeltaRpm`) immediately evaluates to true, setting JUMP on the very first tick, so `isHealthy()` returns false before any real pulse is counted.
- **Trigger:** Runtime config reload with `"n1": { "ppr": 0 }` or any path that calls `begin(pin, 0.0f)`.
- **Impact:** N1 RPM permanently reads as unhealthy from the moment the bad config is applied. SafetyMonitor enters `limpMode`. True N1 speed is invisible to all safety checks, including overspeed.
- **Evidence:** `src/hal/sensors/PCNTRpmSensor.h:85`: no guard before division. `src/system/HardwareConfig.cpp:906`: `n1RpmPpr = n1["ppr"] | n1RpmPpr` replaces the value with whatever JSON supplies including 0.
- **Suggested fix:** In `begin(int pin, float ppr)`, clamp `_ppr = max(ppr, 0.001f)` and log a warning. Additionally add a runtime validation that rejects `ppr <= 0` in the config loader.
- **Confidence:** High

---

### ECU-SEN-05: NTCSensor returns garbage float for invalid calibration parameters without flagging unhealthy
- **Severity:** Medium
- **Bug class:** Input validation / Error handling
- **Location:** `src/hal/sensors/NTCSensor.h:55-56`
- **Description:** `getValue()` computes `invT = (1.0f / t0K) + (1.0f / _cal.beta) * logf(r / _cal.r0)`. Three cases produce silent bad output while `isHealthy()` returns true (because `_railCheck()` only checks ADC level):
  1. `_cal.beta == 0`: `1.0f / 0.0f` = `+Inf`; `invT` becomes `Inf` or `NaN`; `1.0f / invT - 273.15f` = `-273.15` or `NaN`.
  2. `_cal.r0 == 0`: `logf(r / 0)` = `logf(+Inf)` = `+Inf`; same result.
  3. `_cal.rFixed == 0`: `r = 0`; `logf(0)` = `-Inf`; `invT` = `-Inf`; result = `-273.15`.
  In all three cases the ADC is in a normal mid-range position so `_railCheck()` passes and `isHealthy()` returns true. The garbage temperature propagates to `ed.oilTemp` with `ed.oilTempHealthy = true`.
- **Trigger:** Misconfigured JSON sets `ntc_r0: 0` or `ntc_beta: 0`. The NTC value passes through `Hardware::initSensors` at line 608 without validation.
- **Impact:** `oilTempHealthy = true` with `oilTemp = -273.15` or `NaN`. The oil-temperature safety check (`SafetyMonitor.h:185`) fires because `-273.15 > oilTempLimit` is false but `NaN` comparisons are false, so the limit is never tripped. A genuinely overheating engine with a misconfigured NTC would go undetected.
- **Evidence:** `src/hal/sensors/NTCSensor.h:55`: unguarded `logf`. `src/hal/sensors/NTCSensor.h:59`: `isHealthy()` does not check the computed temperature for plausibility. `src/Hardware.h:608`: no validation of NTC cal values.
- **Suggested fix:** In `getValue()`, return `-999.0f` (and let `isHealthy()` detect it) if `_cal.beta <= 0.0f || _cal.r0 <= 0.0f || _cal.rFixed <= 0.0f`. Also add a sanity clamp on the final temperature (e.g., reject values outside -60..200 degC for an oil-temp application) and propagate that as a health failure.
- **Confidence:** High

---

### ECU-SEN-06: AnalogLinearSensor extrapolates silently above calibrated range
- **Severity:** Medium
- **Bug class:** Input validation / Logic bug
- **Location:** `src/hal/sensors/AnalogSensor.h:119-128` (`AnalogLinearSensor::getValue`)
- **Description:** `getValue()` does not clamp `raw` to `[_cal.rawMin, _cal.rawMax]` before interpolating. `_railCheck()` admits ADC values from 10 to 4084. If the calibration endpoint `_cal.rawMax` is set lower than 4084 (e.g., `rawMax = 3700` for a 0.5-4.5 V transducer on a 3.3 V ADC), readings between 3700 and 4084 produce extrapolated values above `valMax` while `isHealthy()` returns true. For a fuel-pressure sensor calibrated to 6 bar at raw 3700, raw 4084 returns approximately 6.6 bar - a value that appears physically real but is extrapolated outside the characterized range.
- **Trigger:** Any AnalogLinear sensor (fuel pressure, fuel flow, P1, P2, battery voltage) where `rawMax < 4085` and the transducer drives the ADC higher than `rawMax`.
- **Impact:** Fuel or oil pressure reading silently exceeds the calibrated maximum while `fuelPressHealthy = true`. Safety limits based on this value could be set in the wrong direction. Conversely, a reading below `rawMin` extrapolates negative, which could give a false low-pressure alarm.
- **Evidence:** `src/hal/sensors/AnalogSensor.h:120-123`: no clamping of `raw` before linear interpolation. Compare with `AnalogPolySensor::getValue()` at line 98 which does `constrain(x, _cal.xMin, _cal.xMax)`.
- **Suggested fix:** Add `raw = constrain(raw, _cal.rawMin, _cal.rawMax)` before the interpolation, matching the existing pattern in `AnalogPolySensor`. Optionally, also add `if (raw <= (float)_cal.rawMin || raw >= (float)_cal.rawMax) _outOfRange = true` to propagate a health warning for out-of-range inputs.
- **Confidence:** High

---

### ECU-SEN-07: RC idle channel stale value persists after failsafe without being zeroed
- **Severity:** Medium
- **Bug class:** Fail-safe logic
- **Location:** `src/hal/RCInput.h:56-61` (`RCInput::tick`), `src/engine/sequencer/blocks/ModifiedIdle.h:30`
- **Description:** When `OT_IDLE_INPUT_RC_PWM` is defined, `RCInput::tick()` writes `ed.idleInputRaw = (int)(ed.rcIdleNorm * 4095.0f)` only when `ed.rcIdleValid` is true (line 58-61). When the RC link is lost and `rcIdleValid` goes false, `idleInputRaw` retains the last commanded value. `ModifiedIdle` and `FuelPumpIdle` sequence blocks read `ed.idleInputRaw` without checking `ed.rcIdleValid`, so they continue to apply the stale idle setpoint indefinitely after link loss.
- **Trigger:** RF interference or transmitter power-off during an active idle-adjust sequence with RC idle input configured.
- **Impact:** Engine idles at the last commanded level rather than falling back to a safe minimum. The failsafe for the main throttle channel (line 1166 in Hardware.h) correctly zeroes throttle on link loss, but the idle trim path has no equivalent. Depending on where `idleInputRaw` was when the link was lost, this could leave the engine idling higher than intended.
- **Evidence:** `src/hal/RCInput.h:58-61`: write guarded by `rcIdleValid`. `src/engine/sequencer/blocks/ModifiedIdle.h:30`: `constrain(ed.idleInputRaw / 4095.0f, ...)` - no `rcIdleValid` check. Contrast with throttle path at `src/Hardware.h:1166`: `norm = (ed.rcThrottleValid) ? ed.rcThrottleNorm : 0.0f`.
- **Suggested fix:** In `RCInput::tick()`, when `rcIdleValid` is false, explicitly set `ed.idleInputRaw = 0` (or the minimum safe idle count) to match the throttle failsafe behavior. Or add a `rcIdleValid` check in `ModifiedIdle` and `FuelPumpIdle` before using `idleInputRaw`.
- **Confidence:** High

---

### ECU-SEN-08: MAX6675 ring buffer not cleared on sensor fault; stale samples skew reading after reconnect
- **Severity:** Medium
- **Bug class:** Error handling / Logic bug
- **Location:** `src/hal/sensors/MAX6675TempSensor.h:53-64` (`update`)
- **Description:** When a bad SPI read is detected (NaN, `<= 0`, or `> 1023.75`), `update()` sets `_healthy = false` and returns immediately without clearing the ring buffer. When the sensor recovers, the first good sample is pushed into the buffer alongside up to 5 stale pre-fault samples. The rolling average over `_filled` items then mixes old and new data. With `NUM_AVG = 6` and `READ_INTERVAL_MS = 250 ms`, recovery to a fully-fresh average takes 1500 ms.
- **Trigger:** Momentary SPI glitch (e.g., during igniter firing), transient open circuit, or brief brownout. The sensor reports bad for one or two reads, then recovers.
- **Impact:** For 1-2 seconds after recovery, `ed.tot` is a weighted mix of pre-fault and current temperatures. If the thermocouple disconnected on a cooling engine and reconnects on a hot one (or vice versa), the average understates or overstates the real TOT for that window. During startup this could delay or prematurely trigger TOT alarm thresholds.
- **Evidence:** `src/hal/sensors/MAX6675TempSensor.h:54-56`: early return without touching `_buf`, `_idx`, or `_filled`. `src/hal/sensors/MAX6675TempSensor.h:36-37`: buffer cleared only in `begin()`.
- **Suggested fix:** On a fault detection, reset the ring buffer (`_filled = 0; _idx = 0;`) so that the first post-recovery read initializes from a clean state. This trades the 1.5 s stale-average period for a single-sample reading on recovery, which is the more trustworthy behavior.
- **Confidence:** High

---

### ECU-SEN-09: PCNTRpmSensor::begin() leaks PCNT unit handle on repeated initialization
- **Severity:** Medium
- **Bug class:** Memory safety / Resource management
- **Location:** `src/hal/sensors/PCNTRpmSensor.h:29-67` (`begin`)
- **Description:** `begin()` unconditionally calls `pcnt_new_unit()`, which allocates one of the ESP32's four hardware PCNT units. There is no call to `pcnt_unit_stop()`, `pcnt_unit_disable()`, or `pcnt_del_unit()` before creating a new unit. If `begin()` is called more than once on the same sensor object (e.g., via a future runtime reconfiguration path, or if `Hardware::initSensors()` is called again), the old unit handle is orphaned and the driver's internal unit slot remains occupied.
- **Trigger:** Any code path that calls `g_sensorN1Rpm.begin(...)` a second time after the initial hardware init.
- **Impact:** With three PCNT sensors in use (N1, N2, fuel-flow pulse), the ESP32 has only one free PCNT unit. A single double-init would exhaust all units; subsequent `pcnt_new_unit()` calls return `ESP_ERR_NOT_FOUND`, which `ESP_ERROR_CHECK` escalates to a panic/reboot. This manifests as a hard crash when attempting config reload.
- **Evidence:** `src/hal/sensors/PCNTRpmSensor.h:39`: `ESP_ERROR_CHECK(pcnt_new_unit(&unitCfg, &_unit))` with no preceding teardown. `_unit` is overwritten without freeing the previous handle.
- **Suggested fix:** Add a teardown guard at the top of `begin()`:
  ```cpp
  if (_unit) {
      pcnt_unit_stop(_unit);
      pcnt_unit_disable(_unit);
      pcnt_del_unit(_unit);
      _unit = nullptr;
  }
  ```
- **Confidence:** High

---

### ECU-SEN-10: RCInput ISR writes pulseUs then fresh without a compiler barrier; potential torn read on Xtensa
- **Severity:** Low
- **Bug class:** Concurrency
- **Location:** `src/hal/RCInput.h:91-93`, `src/hal/RCInput.h:100-102`
- **Description:** The ISR writes `ch.pulseUs = pw` then `ch.fresh = true` (two separate volatile stores). The C++ standard guarantees that the compiler will not reorder two accesses to the same volatile object, but it does not guarantee ordering between accesses to *different* volatile objects (`pulseUs` and `fresh` are separate members). A sufficiently aggressive compiler could emit the `fresh = true` store before the `pulseUs` store. On the same Xtensa core, hardware store ordering is preserved, but this depends on an implementation guarantee rather than a language guarantee. The reader in `_updateCh` tests `ch.fresh` and then reads `ch.pulseUs` without any barrier.
- **Trigger:** Compiler optimization level -O2 or higher with a future toolchain that legally reorders across different volatile members.
- **Impact:** Main loop reads `fresh = true` but sees the previous `pulseUs` value -> uses a one-tick-stale pulse width for normalization. Throttle position glitches by one sample. With a 50 Hz RC frame rate this is 20 ms of stale data, which is within normal RC latency tolerance but is technically incorrect.
- **Evidence:** `src/hal/RCInput.h:91-93`: `_thr.pulseUs = pw; _thr.fresh = true;` - no `__asm__ volatile("" ::: "memory")` or `std::atomic_thread_fence` between them.
- **Suggested fix:** Add a compiler barrier between the two writes in the ISR: `__asm__ volatile("" ::: "memory");` between `pulseUs = pw` and `fresh = true`. Or convert `Ch` members to `std::atomic<uint32_t>` and `std::atomic<bool>` with `memory_order_release` / `memory_order_acquire` semantics.
- **Confidence:** Medium (correct on current ESP-IDF toolchain; violates strict C++ memory model)

---

### ECU-SEN-11: MAX31856 SPI mode comment is incorrect (says mode 1, implements mode 3)
- **Severity:** Low
- **Bug class:** Documentation / Logic bug
- **Location:** `src/hal/sensors/MAX31856TempSensor.h:11`, `src/hal/sensors/MAX31856TempSensor.h:117`
- **Description:** The file header says `Datasheet SPI: CPOL=1, CPHA=1 (mode 1 or 3 -- reads on falling SCLK, MAX31856 is mode 1)` and the inline comment at line 117 says `// MAX31856 is SPI mode 1 (CPOL=0,CPHA=1)`. SPI mode 1 is CPOL=0 (idle LOW), CPHA=1. The implementation initializes CLK idle HIGH (`digitalWrite(_clk, HIGH)`) which is CPOL=1; it samples MISO on the falling edge (clock goes LOW then reads). This is SPI mode 3 (CPOL=1, CPHA=1), which is what the MAX31856 datasheet actually requires. The code behavior is correct for the hardware; the comments are wrong.
- **Trigger:** A developer following the comment and wiring a logic analyzer expecting CLK to idle LOW would misread the capture. Or, if ported to hardware-SPI, the wrong mode could be selected based on the comment.
- **Evidence:** `src/hal/sensors/MAX31856TempSensor.h:41`: `digitalWrite(_clk, HIGH);  // CPOL=1 idle high`. `src/hal/sensors/MAX31856TempSensor.h:124-128`: clock goes LOW -> sample -> clock goes HIGH = mode 3 bit-bang.
- **Suggested fix:** Update the comment to read: `// MAX31856 uses SPI mode 3 (CPOL=1, CPHA=1): SCLK idles HIGH, data sampled on falling edge.`
- **Confidence:** High

---

## Notes / unclear areas

1. **ZERO_STUCK vs SATURATED overlap during coastdown:** When the engine spins down through the 500-2000 RPM band, both SATURATED (ECU-SEN-02) and potentially ZERO_GLITCH can be set in the same tick. The caller (`SafetyMonitor`) sees `isHealthy() = false` from `_health.any()`, but the distinction between a legitimate stop and a sensor fault is lost. A separate "engine-stopping-normally" mode flag could resolve this.

2. **`AnalogPolySensor` clamping introduces a silent plateau:** `constrain(x, xMin, xMax)` prevents extrapolation beyond the cal range but causes the polynomial to saturate at `poly(xMin)` or `poly(xMax)` for any out-of-range input. If oil pressure exceeds the calibrated maximum, the sensor continues to report `poly(xMax)` without any health indication that the reading is clamped. This was judged low severity given `_railCheck()` already rejects true rail values, but operators should be aware the reading will plateau rather than climb.

3. **`DS18B20TempSensor::begin()` override calls `begin(_pin, _resolution)` with `_pin = -1` if no runtime init was done first:** This is a footgun if someone calls `ISensor::begin()` on a freshly-constructed object before calling `begin(pin, resolution)`. The current codebase never triggers this path, but a future refactor that iterates sensors polymorphically and calls `begin()` on each would hit it.

4. **`fuelFlowPulse` sensor (PCNTRpmSensor with `ppr = 1.0`) inherits all PCNT issues** including ECU-SEN-04 (ppr=0 guard) and ECU-SEN-09 (double-init). The `pulsesPerLitre` guard at `Hardware.h:780` protects the flow-rate calculation, but the underlying RPM sensor can still produce `Inf` if `ppr` is set to 0 before being passed into the constructor.

5. **`MockSensor::_rampRate` is not guarded:** In DEV_MODE, `setValue()` and `setRamp()` are callable from the web UI. A large `rampRate` can push `_value` to any float within one session, potentially exercising safety-monitor code paths with impossible sensor readings. This is intentional for testing, but there is no compile-time enforcement that `MockSensor` cannot be compiled into a release build.
