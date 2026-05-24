# Boot-init subsystem audit

## Files reviewed

| File | Lines examined |
|------|---------------|
| `src/main.cpp` | 1498-1616 (setup()), 86-270 (buildSequences/validateSequences), 1112-1155 (handleCommand START) |
| `src/platform/esp32/PlatformInit.h` | full |
| `src/platform/esp32/StatusLED.h` | full |
| `hardware_profile.h` | full |
| `partitions.csv` | full |
| `src/Hardware.h` | full (applyConfig, initSensors, initActuators, updateActuators, allOff) |
| `src/hal/actuators/RelayActuator.h` | full |
| `src/hal/actuators/ServoActuator.h` | full |
| `src/hal/actuators/LEDCActuator.h` | full |
| `src/system/HardwareConfig.h` | full |
| `src/system/HardwareConfig.cpp` | full (load, save, applyDefaults) |
| `src/system/Config.h` | full |
| `src/system/Config.cpp` | 225-330 (load) |
| `src/system/FlightRecorder.h` | full |
| `src/system/FlightRecorder.cpp` | begin, recordCount, runEviction |
| `src/system/web/WebServer.cpp` | begin, tick, 1102-1135 |
| `src/system/ClusterSerial.cpp` | begin() |
| `src/hal/RCInput.h` | full |
| `src/engine/SafetyMonitor.h` | begin() |
| `src/engine/EngineData.h` | mode default |

---

## Findings (Critical -> Info)

---

### ECU-BOOT-01: Actuator GPIO pins float from power-on reset until initActuators sets them LOW

- **Severity:** Critical (hardware-dependent; becomes High if external gate pull-downs are guaranteed)
- **Bug class:** Fail-safe logic / initial GPIO state
- **Location:** `src/Hardware.h` `initActuators()` (line 824); `src/main.cpp` setup() line 1545
- **Description:**
  From power-on until `Hardware::initActuators()` is called at `main.cpp:1545`, no firmware code configures the actuator GPIO pins as outputs or drives them to a known safe level. The pin states are entirely determined by the ESP32 hardware reset state (all GPIO default to INPUT, high-impedance) and any external pull resistors. The relay/MOSFET gate on each actuator pin has no driven-low guarantee from the firmware side during the following window: `PlatformInit::begin()` -> `HardwareConfig::load()` -> `buildSequences()` -> `Config::load()` -> `Hardware::applyConfig()` -> `FlightRecorder::begin()` -> `SessionLogger::begin()` -> `CommandQueue::begin()` -> `Hardware::initSensors()`. This covers approximately 200-500 ms of boot time depending on LittleFS read speed.
- **Trigger:** Every power-on or hard reset.
- **Impact:**
  - `OT_IGNITER_PIN` (GPIO 21, `hardware_profile.h:179`, active-high, `OT_IGNITER_ACTIVE_H = true`): GPIO 21 has no internal pull-up or pull-down on the ESP32. If the external relay/MOSFET gate lacks a pull-down resistor, the floating GPIO 21 can hold a random voltage, potentially energising the igniter coil or relay during boot before `initActuators()` drives it LOW.
  - `OT_STARTER_EN_PIN` (GPIO 25, `hardware_profile.h:192`, active-high): same floating condition. A spurious HIGH could close the starter enable relay and pre-charge the starter ESC capacitors.
  - `OT_OIL_PUMP_PIN` (GPIO 4, LEDC PWM, `hardware_profile.h:154`): floats as INPUT; LEDC peripheral uninitialised. External gate state depends solely on pull resistor.
  - `OT_FUEL_SOL_PIN` (GPIO 12, `hardware_profile.h:174`, active-high): GPIO 12 is the ESP32 MTDI strapping pin and has an internal weak pull-DOWN, so it boots LOW. The fuel solenoid therefore boots CLOSED (inactive, safe) for active-high configurations. However, if `hw.fuelSolActiveH` is overridden to `false` in `hardware.json`, a LOW pin at boot opens the solenoid before firmware has a chance to close it.
- **Evidence:**
  - `RelayActuator::begin()` at `src/hal/actuators/RelayActuator.h:21-24`: `pinMode(pin, OUTPUT); off();` -- this is the only place the pin is driven LOW. It is not called until `Hardware::initActuators()`.
  - `ServoActuator::begin()` at `src/hal/actuators/ServoActuator.h:20-25`: writes `_minUs` on attach -- also only in `initActuators()`.
  - `LEDCActuator::begin()` at `src/hal/actuators/LEDCActuator.h:37-40`: `ledcWrite(pin, 0)` -- also only in `initActuators()`.
  - `OT_DECLARE_HARDWARE` in `src/Hardware.h:143-169`: constructors only copy parameters into member fields; none touch GPIO.
- **Suggested fix:**
  1. Add a `Hardware::assertActuatorsOff()` function that iterates every known actuator GPIO from `HardwareConfig` and calls `pinMode(pin, OUTPUT); digitalWrite(pin, LOW);` using the `fuelSolActiveH`/`igniterActiveH` polarity to choose the correct inactive level. Call this as the very first action after `HardwareConfig::load()` at `main.cpp:1505`, before any other setup step.
  2. For relay and MOSFET circuits, require a gate pull-down (10 kohm typical) on every actuator output in the PCB design guide. Document this as a build requirement alongside the strapping pin advisory in `hardware_profile.h`.
- **Confidence:** High

---

### ECU-BOOT-02: OT_FUEL_SOL_PIN = GPIO 12 is the ESP32 MTDI strapping pin; active-low configuration creates a spurious-open risk at boot

- **Severity:** High
- **Bug class:** Fail-safe logic / strapping pin interaction
- **Location:** `hardware_profile.h:174-175`; `src/system/HardwareConfig.cpp:187-188`
- **Description:**
  GPIO 12 is assigned as `OT_FUEL_SOL_PIN` with `OT_FUEL_SOL_ACTIVE_H = true` by default. The ESP32 samples GPIO 12 (MTDI) at reset to select flash voltage: a LOW level selects the internal 3.3 V regulator (normal), a HIGH level selects an external 1.8 V regulator. The internal weak pull-DOWN keeps GPIO 12 LOW at boot, which is safe for an active-high fuel solenoid. However, if a `hardware.json` file sets `fuelSolActiveH: false` (active-low solenoid), the LOW boot level represents the ACTIVE state, causing the fuel solenoid to open before `initActuators()` can close it. Additionally, if any external pull-up is added to GPIO 12 for PCB routing reasons, the flash voltage selection will be corrupted, causing an immediate boot failure or flash damage on 3.3 V flash modules.
- **Trigger:** Operator changes `fuelSolActiveH` to `false` in `hardware.json`, OR any external pull-up is present on GPIO 12.
- **Impact:** Fuel solenoid opens during boot before engine is initialised. Combined with a powered fuel pump (e.g., residual pressure), this can result in uncontrolled fuel flow. Separately: corrupted flash voltage selection causes boot loop or flash wear.
- **Evidence:**
  - `hardware_profile.h:174-175`: `#define OT_FUEL_SOL_PIN 12` / `#define OT_FUEL_SOL_ACTIVE_H true`
  - `hardware_profile.h:459-467`: strapping pin advisory explicitly lists GPIO 12 as MTDI, recommends avoidance.
  - `src/system/HardwareConfig.cpp:187-188`: `HardwareConfig::fuelSolPin = OT_FUEL_SOL_PIN; HardwareConfig::fuelSolActiveH = OT_FUEL_SOL_ACTIVE_H;` -- defaults match compile-time, but JSON can override polarity.
- **Suggested fix:**
  Reassign `OT_FUEL_SOL_PIN` to a non-strapping GPIO (e.g., GPIO 26 on ESP32, avoiding GPIOs 6-11 and 12). If GPIO 12 must be retained for PCB layout reasons, add a compile-time `#error` in `hardware_profile.h` when `OT_FUEL_SOL_PIN == 12`, mirroring the existing flash-pin guards. Add a runtime check in `HardwareConfig::load()` that logs a critical warning if the loaded `fuelSolPin == 12`.
- **Confidence:** High

---

### ECU-BOOT-03: webTask spawned before FlightRecorder::begin() -- toJson/lockLog/logConfigChange race window

- **Severity:** Medium
- **Bug class:** Concurrency / ordering
- **Location:** `src/main.cpp:1598` (webTask spawn), `src/main.cpp:1540` (FlightRecorder::begin()), `src/system/web/WebServer.cpp:1113` (tick calls runEviction), `src/system/FlightRecorder.cpp:27`
- **Description:**
  `xTaskCreatePinnedToCore(webTask, ...)` at `main.cpp:1598` starts a FreeRTOS task on Core 0 that immediately calls `WebServer::begin()` (WiFi + route registration) and then loops calling `WebServer::tick()`. `FlightRecorder::begin()` is called on Core 1 at `main.cpp:1540`, which is AFTER the webTask is spawned. The mutex `FlightRecorder::_mutex` is created inside `begin()` at `FlightRecorder.cpp:27`. Between task spawn and `FlightRecorder::begin()`, the webTask may call:
  - `WebServer::tick()` -> `FlightRecorder::runEviction()` (line 1113): this is safe at boot because `_evictionPending` is `false` (static initialiser), so it returns immediately.
  - A WebSocket client connecting during this window could trigger `_buildTelemetry()` -> `doc["log_records"] = FlightRecorder::recordCount()` (WebServer.cpp:171): `recordCount()` reads `s_lineCount` without the mutex but does not write. `s_lineCount` is initialised to `0` (static). This is safe.
  - A POST to `/api/config` during this window triggers `FlightRecorder::logConfigChange()` (WebServer.cpp:539), which calls `_append()`, which attempts `xSemaphoreTake(_mutex, ...)` where `_mutex == nullptr`. The null-check `if (_mutex) xSemaphoreTake(...)` at `FlightRecorder.cpp:200` prevents a crash, but the log entry is silently dropped.
  The practical risk is low because WiFi association takes several hundred milliseconds and `FlightRecorder::begin()` runs early in `setup()`. However, in a fast reboot or warm WiFi reconnect scenario, the window is real.
- **Trigger:** A web client sends a config PATCH request within the ~100-200 ms window between webTask spawn and `FlightRecorder::begin()`.
- **Impact:** Config-change log entry silently dropped; no data corruption, no crash.
- **Evidence:**
  - `main.cpp:1598`: `xTaskCreatePinnedToCore(webTask, ...)` before `main.cpp:1540`: `FlightRecorder::begin()`.
  - `FlightRecorder.cpp:27`: `if (!_mutex) _mutex = xSemaphoreCreateMutex();`
  - `WebServer.cpp:539`: `FlightRecorder::logConfigChange(...)` inside a PATCH handler that runs in async_tcp context (Core 0).
- **Suggested fix:**
  Move `FlightRecorder::begin()` (and `SessionLogger::begin()` / `CommandQueue::begin()`) to immediately after `PlatformInit::begin()`, before any file I/O or task spawning. Alternatively, move the webTask spawn to after all `begin()` calls, i.e., swap lines 1540-1546 and 1598. The simplest one-line fix: move `xTaskCreatePinnedToCore` to after `Hardware::initActuators()` (line 1545).
- **Confidence:** High

---

### ECU-BOOT-04: LittleFS double-failure (mount AND format fail) is silent; subsequent save() calls fail silently

- **Severity:** Medium
- **Bug class:** Error handling
- **Location:** `src/platform/esp32/PlatformInit.h:23-27`; `src/system/HardwareConfig.cpp:344-346`; `src/system/Config.cpp:261-266`
- **Description:**
  `LittleFS.begin(true)` attempts to mount the filesystem and, on failure, attempts to format it. If the format also fails (corrupted flash, hardware fault), `begin()` returns `false` but `PlatformInit::begin()` logs only `"LittleFS mount failed -- formatted"` and continues. The filesystem is NOT mounted. All subsequent `LittleFS.exists()`, `LittleFS.open()`, and `LittleFS.mkdir()` calls silently fail or return empty results. The effects:
  1. `HardwareConfig::load()` calls `applyDefaults()` then tries `save()`. `save()` calls `LittleFS.open(PATH, "w")` which fails silently, returning an invalid `File`. The save is lost; no error is propagated.
  2. `Config::load()` finds no file (because `LittleFS.exists()` returns false on unmounted FS) and falls through to the defaults path, which calls `save()` -- same silent failure.
  3. `FlightRecorder::begin()` calls `LittleFS.mkdir("/logs")` -- silently no-ops.
  4. Boot continues with compiled defaults and no config persistence. The operator has no indication beyond the single serial line that something is wrong.
  The `partitions.csv` uses `subtype=spiffs` for the filesystem partition (line 7); this is correct -- the LittleFS Arduino library mounts a partition by type `data/spiffs` -- so the partition label is not the problem.
- **Trigger:** Flash wear-out, soldering defect, or ESD event causing internal flash corruption.
- **Impact:** ECU runs indefinitely on factory defaults; any user calibration (oil polynomial, RPM limits, safety thresholds) is lost. The operator does not receive an audible, visual, or web-visible fault indicator. If the user recalibrates settings during the session, those saves silently fail too.
- **Evidence:**
  - `PlatformInit.h:23-27`: single `if (!LittleFS.begin(true))` branch with no halt or second-stage error.
  - `HardwareConfig.cpp:344-347`: `if (!LittleFS.exists(PATH)) { save(); return; }` -- `save()` return value discarded.
  - `Config.cpp:261-266`: same pattern; `save()` return value discarded.
- **Suggested fix:**
  1. After `LittleFS.begin(true)` returns `false`, set a global `g_fsMountFailed` flag and populate `EngineData::instance().faultDescription` with a user-visible message.
  2. Check `g_fsMountFailed` in `WebServer::_buildTelemetry()` and expose it as a top-level `"fs_error"` field so the web dashboard shows a banner.
  3. In `HardwareConfig::save()` and `Config::save()`, check the return value of `LittleFS.open()` and, on failure, set the flag and log `[ERROR]`.
- **Confidence:** High

---

### ECU-BOOT-05: Dead code -- setup() FAULT mode check can never be true; stale Config.h comment

- **Severity:** Medium
- **Bug class:** Logic bug / misleading documentation
- **Location:** `src/main.cpp:1516-1519`; `src/system/Config.h:11`
- **Description:**
  `Config.h:11` documents: `"If profile_id mismatch -> set mode=FAULT, block engine ops"`. In reality, `Config::load()` never assigns `EngineData::instance().mode = SysMode::FAULT`. On a profile mismatch, it sets `Config::profileMatch = false` and writes `faultDescription`. `EngineData::mode` defaults to `SysMode::STANDBY` (EngineData.h:110) and nothing between `PlatformInit::begin()` and the check at `main.cpp:1516` can change it to `FAULT`. Therefore the branch `if (EngineData::instance().mode == SysMode::FAULT)` at line 1516 is dead code: it never executes. The real engine-ops lock is the `Config::profileMatch` gate inside `handleCommand(START)` at line 1117.
  This creates a maintenance hazard: a developer reading the comment may believe that profile mismatches produce `FAULT` mode behaviour (rapid LED flash, MAVLink `EMERGENCY` state, etc.) when they do not.
- **Trigger:** Every boot with a profile-ID mismatch in the config file.
- **Impact:** No safety impact at runtime. Developer confusion; any future code that relies on mode==FAULT being set by a config mismatch will be wrong.
- **Evidence:**
  - `Config.h:11`: comment states `mode=FAULT`.
  - `Config.cpp:270-317`: all mismatch paths set `profileMatch = false`; none assign `mode`.
  - `EngineData.h:110`: `volatile SysMode mode = SysMode::STANDBY;`
  - `main.cpp:1516`: `if (EngineData::instance().mode == SysMode::FAULT)` -- unreachable.
  - `main.cpp:1117`: `if (ed.mode == SysMode::STANDBY && Config::profileMatch)` -- the actual gate.
- **Suggested fix:**
  Option A (minimal): Remove the dead check at `main.cpp:1516-1519` and update `Config.h:11` to read `"If profile_id mismatch -> set profileMatch=false, block START command"`.
  Option B (preferred for visibility): In `Config::load()`, when `profileMatch` is set to `false` due to a file-open or JSON-parse failure (lines 279, 293), also set `EngineData::instance().mode = SysMode::FAULT`. This aligns code with the documented intent and gives the operator a visual FAULT indicator (rapid LED, MAVLink EMERGENCY).
- **Confidence:** High

---

### ECU-BOOT-06: stopPin race window -- compile-time OT_STOP_PIN configured INPUT_PULLUP before runtime pin reassignment

- **Severity:** Low
- **Bug class:** State machine / ordering
- **Location:** `src/platform/esp32/PlatformInit.h:61-64`; `src/main.cpp:1511-1512`
- **Description:**
  `PlatformInit::begin()` configures the compile-time `OT_STOP_PIN` (GPIO 15) and `OT_START_PIN` (GPIO 13) as `INPUT_PULLUP` at PlatformInit.h:61-64. After `HardwareConfig::load()` reads `hardware.json`, `main.cpp:1511-1512` re-applies `pinMode()` using the runtime `hcfg.stopPin` and `hcfg.startPin`. If the runtime values differ from the compile-time values (e.g., the operator changed the pin assignment in `hardware.json`), there is a window from `PlatformInit::begin()` to line 1511 where:
  - The OLD compile-time pin is configured as `INPUT_PULLUP` (no harm).
  - The NEW runtime pin is in its power-on state (INPUT, high-Z, no pull).
  If `stopPullup = false` is read from `hardware.json` but `stopActiveH = false` (active-low stop), the runtime pin will be floating INPUT without a pullup. Any noise during `HardwareConfig::load()` (JSON parsing takes CPU time) could assert a spurious stop. More importantly, if the runtime `stopPin` value in a corrupt `hardware.json` is an invalid GPIO number (e.g., a large integer from a truncated JSON write), `pinMode()` at line 1511 is called with that value, which is undefined behaviour on Arduino ESP32 (the `gpio_set_direction` IDF call may silently fail or panic).
- **Trigger:** `hardware.json` contains a `stopPin` or `startPin` value different from the compile-time default, OR a corrupt `hardware.json` produces an out-of-range pin value.
- **Impact:** Spurious stop event on boot (Low), or undefined behaviour / watchdog reset from invalid GPIO number (Medium).
- **Evidence:**
  - `PlatformInit.h:61`: `pinMode(OT_STOP_PIN, INPUT_PULLUP);`
  - `main.cpp:1511`: `pinMode(hcfg.stopPin, hcfg.stopPullup ? INPUT_PULLUP : INPUT);` -- no range check on `hcfg.stopPin`.
  - `HardwareConfig.cpp:894`: `stopPin = ctrl["stop_pin"] | stopPin;` -- ArduinoJson `|` operator uses default on missing key but accepts any integer value present.
- **Suggested fix:**
  Add pin range validation in `HardwareConfig::load()` or `_fromDoc()`: for safety-critical pins (`stopPin`, `startPin`), clamp to `[0, 39]` (ESP32) or `[0, 48]` (S3) and log an error if out of range. Revert to the compile-time default if invalid. Example: `if (stopPin < 0 || stopPin > 39) { stopPin = OT_STOP_PIN; Serial.println("[HWCfg] Invalid stopPin -- using default"); }`.
- **Confidence:** Medium

---

### ECU-BOOT-07: NVS Preferences failure is fully silent -- bootCount silently stays 0

- **Severity:** Low
- **Bug class:** Error handling / persistence
- **Location:** `src/platform/esp32/PlatformInit.h:34-38`
- **Description:**
  `Preferences::begin("ot", false)` opens the NVS namespace. If NVS is corrupt or the NVS partition is full, `begin()` returns `false` and all subsequent `getUInt()`/`putUInt()` calls silently return default values (0) or silently fail. There is no check on the return value of `prefs.begin()` or `prefs.putUInt()`. The ECU continues to operate with `EngineData::bootCount = 1` forever, losing boot-count persistence. More importantly, there is no indication in serial output or EngineData that NVS is unavailable.
- **Trigger:** NVS partition wear-out or flash corruption.
- **Impact:** `bootCount` telemetry is unreliable. No operational safety impact, but diagnostic value of `bootCount` and `resetReason` persistence is lost silently.
- **Evidence:**
  - `PlatformInit.h:34-38`: `prefs.begin("ot", false)` return value ignored; `prefs.putUInt()` return value ignored.
- **Suggested fix:**
  ```cpp
  if (!prefs.begin("ot", false)) {
      Serial.println("[OT] WARNING: NVS unavailable -- bootCount not persisted");
      EngineData::instance().nvsFailed = true; // expose in telemetry
  } else {
      // existing read/write
  }
  ```
- **Confidence:** High

---

### ECU-BOOT-08: ClusterSerial::begin() uses blocking delay(100) x3 with webTask already running, before watchdog

- **Severity:** Low
- **Bug class:** Timing / concurrency hygiene
- **Location:** `src/system/ClusterSerial.cpp:90-93`; `src/main.cpp:1603`
- **Description:**
  `ClusterSerial::begin()` at `ClusterSerial.cpp:90-93` loops `for (int rep = 0; rep < 3; rep++) { _sendSchema(); delay(100); }`, blocking Core 1 for 300 ms. This happens at `main.cpp:1603`, after `xTaskCreatePinnedToCore(webTask, ...)` at line 1598 has already started the web task on Core 0. During this 300 ms block:
  - Core 0 (webTask) is running, handling WiFi events, WebSocket push, and DNS.
  - Core 1 (setup()) is blocked in `delay()`, which calls `vTaskDelay()` internally and yields, but `setup()` itself does not complete.
  - `Watchdog::begin()` has NOT been called yet (line 1613). The ESP32 task watchdog is not yet supervising Core 1.
  This means the 300 ms delay is purely pre-watchdog and cannot cause a WDT reset. The risk is cosmetic: any WebSocket client that connects in this window receives telemetry from Core 0 but Core 1 is not progressing through setup. The comment in `main.cpp:1603` acknowledges this: `"before watchdog (uses delay())"`. The finding is logged for completeness, as the 300 ms latency is measurable on the cluster display and the delay() pattern is not best practice in a FreeRTOS build.
- **Trigger:** `HardwareConfig::hasClusterSerial = true`.
- **Impact:** 300 ms setup latency on Core 1; no safety consequence.
- **Evidence:**
  - `ClusterSerial.cpp:90-93`: `delay(100)` called three times.
  - `main.cpp:1603`: call before `Watchdog::begin()` at line 1613.
- **Suggested fix:**
  Replace the blocking loop with a non-blocking alternative: send the first schema immediately on `begin()` and enqueue the remaining two retransmissions for `ClusterSerial::tick()` using a `millis()`-based state machine. This eliminates the blocking delay and removes the need for the `"before watchdog"` comment.
- **Confidence:** High

---

### ECU-BOOT-09: Hardware::applyConfig() called before initSensors/initActuators -- confirmed safe but ordering is fragile

- **Severity:** Low
- **Bug class:** Ordering / hygiene
- **Location:** `src/main.cpp:1538`; `src/Hardware.h:396-558`
- **Description:**
  `Hardware::applyConfig()` at `main.cpp:1538` is called before `Hardware::initSensors()` (line 1544) and `Hardware::initActuators()` (line 1545). `applyConfig()` only writes to plain member variables of block objects (`g_blkOilPrime.timeoutMs`, etc.), calls calibration setters on concrete sensor objects (`g_sensorOilPress.setCal()`), and writes to `SafetyMonitor` member fields -- none of which are `IActuator*` pointers. The `IActuator*` dispatch pointers (`g_actThrottle`, `g_actOilPump`, `g_actIgniter`, etc.) are `nullptr` until `initActuators()`. `applyConfig()` does not dereference any `IActuator*` pointer. This ordering is therefore safe at present.
  The risk is future maintenance: a developer adding a line to `applyConfig()` that calls `g_actThrottle->set(...)` would introduce a null dereference without any compile-time or runtime guard. The comment at `main.cpp:1574` states `"Safety thresholds are applied via Hardware::applyConfig() above"` without warning about the null-pointer precondition.
- **Trigger:** N/A (safe today).
- **Impact:** Null pointer dereference if `applyConfig()` is extended to write to `IActuator*` pointers before `initActuators()` is called.
- **Evidence:**
  - `Hardware.h:396-558`: `applyConfig()` body -- no `IActuator*` access.
  - `Hardware.h:146`, `151`, `156`, `164`: `IActuator* g_actThrottle = nullptr;` etc.
- **Suggested fix:**
  Add an `assert(g_actThrottle != nullptr)` or a comment block at the top of `applyConfig()` explicitly documenting that the function MUST NOT access `IActuator*` pointers, and that it MUST be called before `initActuators()` solely for the purpose of configuring sequence block parameters.
- **Confidence:** High (safe today; maintenance risk flagged)

---

### ECU-BOOT-10: DI channel pinMode set BEFORE digitalRead for initial state seeding -- correct but relies on implicit ordering

- **Severity:** Info
- **Bug class:** Hygiene
- **Location:** `src/main.cpp:1554-1568`
- **Description:**
  The DI channel initialisation loop at lines 1554-1568 correctly calls `pinMode()` before `digitalRead()`. This is important: reading a floating INPUT pin before setting `INPUT_PULLUP` could seed a wrong debounce state. The code is correct. However, the correct ordering is not documented and there is no compile-time or assertion guard. If the order were reversed in a future edit (e.g., the `digitalRead()` line moved above `pinMode()`), the initial debounce state would be unreliable, potentially triggering a spurious DI edge on the first loop tick.
- **Trigger:** N/A (correct today).
- **Impact:** None at present.
- **Evidence:** `main.cpp:1560`: `pinMode(...)` on line 1560, `digitalRead(...)` on line 1562 -- correct ordering.
- **Suggested fix:** Add a comment above the `digitalRead()` call: `// NOTE: must follow pinMode() -- reading a floating INPUT before pullup is set produces an undefined level.`
- **Confidence:** High

---

## Notes / unclear areas

1. **Strapping pin GPIO 12 (fuel solenoid) external pull-down requirement (ECU-BOOT-01/02):** The firmware analysis shows that GPIO 12 boots LOW due to the MTDI internal pull-down, making the active-high fuel solenoid safe by default. However, this safety is coincidental and depends on the ESP32 silicon behaviour, not on firmware asserting the pin as OUTPUT LOW. The PCB design documentation (if it exists outside this repository) should be audited to confirm gate pull-down resistors are present on GPIO 21 (igniter) and GPIO 25 (starter enable).

2. **`EngineData::mode = SysMode::FAULT` is never set during boot (ECU-BOOT-05):** The FAULT mode exists in the enum and is handled by StatusLED, MAVLink, and ClusterSerial, but no boot code path actually sets it. If a safety intent exists to use FAULT mode for config-load failures, implementing ECU-BOOT-05 Option B is a non-trivial change requiring review of all code that reads `mode == FAULT` to avoid unintended side-effects.

3. **`Config::profileMatch = false` path when BOTH `open()` failures occur (lines 279, 293):** In these cases, `faultDescription` is populated but `profileMatch = false` is the only guard on engine ops. The web dashboard exposes `profile_match` in telemetry (`WebServer.cpp:168`). Operator visibility is adequate if the dashboard is consulted. If the operator never opens the web UI, there is no audible or LED indication of a config load failure.

4. **Partition label `spiffs` used for LittleFS (ECU-BOOT-04 context):** `partitions.csv:7` declares the filesystem partition with `subtype=spiffs`. The ESP32 Arduino LittleFS library searches by type `data` and subtype `spiffs`, so `LittleFS.begin()` correctly targets this partition. No finding here; confirmed compatible.

5. **`buildSequences()` called before `Config::load()`:** Sequence blocks are constructed from `HardwareConfig` data only (pin/feature flags). Config values (timeouts, RPM targets) are applied later via `applyConfig()`. The ordering is correct: sequences are built from topology, configured from tuning params.

6. **`RCInput::begin()` uses compile-time `OT_IDLE_INPUT_PIN` / `OT_THROTTLE_INPUT_PIN` for `attachInterrupt()`:** These are the static `#define` values, not the runtime `HardwareConfig` values. If the operator changes the input pins in `hardware.json`, the ISR is attached to the wrong (compile-time) GPIO. This is a separate bug outside the boot-init scope but worth flagging for completeness.
