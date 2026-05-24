# OpenTurbine ECU — CODEMAP

Recon for the static code audit. All locations cite `file:line`. Scope is source only,
no execution. Project is a personal turbine (jet/turboprop) ECU on ESP32 / ESP32-S3
running Arduino-ESP32 (IDF5). Wireless web UI is the primary tuning surface.

Glossary used below:
- **ISR**: Interrupt Service Routine.
- **IRAM_ATTR**: function placed in instruction RAM (safe from flash cache stalls during ISRs).
- **PCNT**: ESP32 hardware pulse counter peripheral.
- **TWDT**: ESP-IDF Task Watchdog Timer.
- **NVS**: ESP-IDF non-volatile storage (key/value flash).
- **NDJSON**: newline-delimited JSON.
- **LEDC**: ESP32 LED-control PWM peripheral.
- **TOT / TIT**: Turbine Outlet Temperature / Turbine Inlet Temperature.
- **AB**: afterburner.
- **N1 / N2**: gas-generator shaft / power-turbine shaft RPM.

---

## 1. Top-level layout

```
src/
  main.cpp                       1690 lines  — setup(), loop(), mode transitions, command dispatch
  Hardware.h                     ~2000 lines — HAL: sensor/actuator/controller/block global instances + Hardware::* funcs
  engine/
    EngineData.{h,cpp}                       — singleton shared state (sensors, demands, mode, faults, peaks)
    SafetyMonitor.h                          — 12 fault checks, relight FSM, external-fault injection
    Types.h                                  — SysMode, ABMode, RpmHealth, OTCommand enums
    controllers/                             — IController + ThrottleSlew/DynamicIdle/OilPressureLoop/PowerTurbineGovernor
    sequencer/
      SequenceEngine.h                       — block runner (onEnter / tick / onExit / callbacks)
      IBlock.h                               — BlockResult: Running / Complete / Abort / Fault
      blocks/*                               — 30+ block classes (OilPrime, StarterSpin, FuelOpen, FlameConfirm,
                                               TempConfirm, Spool, SafetyHold, ImmediateCut, RPMDrop, CooldownSpin,
                                               ABCheckReady, ABIgnite, ABFlameConfirm, ABStabilize, etc.)
  hal/
    sensors/  — PCNTRpmSensor, MAX6675/31855/31856, DS18B20, NTC, AnalogLinear/Poly/Threshold, MockSensor
    actuators/ — ServoActuator, LEDCActuator, RelayActuator, MockActuator
    RCInput.h  — interrupt-driven RC PWM decoder (2 IRAM ISRs)
  system/
    Config.{h,cpp}                           — settings (gains, limits, calibration, rules)   — LittleFS JSON
    HardwareConfig.{h,cpp}                   — topology (pins, types, sequence names, profile) — LittleFS JSON
    CommandQueue.{h,cpp}                     — FreeRTOS queue (depth 16) of OTPacket
    Watchdog.h                               — TWDT 5 s, single subscriber
    FlightRecorder.{h,cpp}                   — persistent NDJSON ring (2200 records, 20 % evict)
    SessionLogger.{h,cpp}                    — per-run CSV with deferred Core 0 writes
    RulesEngine.h                            — up to 8 sensor→actuator threshold rules
    ClusterSerial.{h,cpp}                    — TX-only JetEcu cluster protocol v2 (UART2)
    MAVLinkOutput.h                          — TX-only MAVLink v1 HEARTBEAT / NAMED_VALUE_FLOAT / STATUSTEXT
    web/WebServer.{h,cpp}                    — 1172 lines: HTTP+WS+OTA+captive portal
  platform/esp32/
    PlatformInit.h                           — boot sequence (LittleFS, ADC config, Preferences, reset reason)
    StatusLED.h                              — millis-based blink FSM
data/                — gzipped HTML/CSS/JS UI (served by WebServer)
hardware_profile.h   — compile-time defaults for pins/features (overridden at runtime by HardwareConfig)
partitions.csv       — 4 MB OTA layout (nvs / otadata / app0 0x180000 / app1 0x180000 / spiffs 0xE0000)
platformio.ini       — esp32dev + esp32s3dev; -DCONFIG_ASYNC_TCP_USE_WDT=0 (workaround)
```

---

## 2. FreeRTOS task / ISR / sync object inventory

### 2.1 Tasks

| Task | Where created | Core | Prio | Stack | Role |
|---|---|---:|---:|---:|---|
| `loopTask` (Arduino default) | implicit, `main.cpp:loop()` | 1 | 1 | 8192 | All control: sensors, sequencer, safety, controllers, actuators, comms tick, logging |
| `web` | `main.cpp:1598` `xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 8, nullptr, 0)` | 0 | 8 | 12288 | WebServer::tick (HTTP + WS + DNS captive). 20 ms delay when engine active, 5 ms in STANDBY |
| `async_tcp` | ESPAsyncWebServer library | (lib default) | 10 | (lib) | Owned by AsyncTCP; serves connections off `web` |

> No per-subsystem tasks. The entire control chain runs serially on `loopTask`.
> See `main.cpp:1618-1689` for loop body ordering.

### 2.2 ISRs

| ISR | File:line | IRAM | Trigger | Effect |
|---|---|:-:|---|---|
| `RCInput::_isrThr` | `src/hal/RCInput.h:87` | yes | `CHANGE` on throttle RC pin | Captures pulse width into `_thr` volatile struct; sets `fresh` flag |
| `RCInput::_isrIdle` | `src/hal/RCInput.h:96` | yes | `CHANGE` on idle RC pin | Captures pulse width into `_idle` volatile struct; sets `fresh` flag |

No software ISR exists for RPM. `PCNTRpmSensor` (`hal/sensors/PCNTRpmSensor.h`) uses
the ESP32 PCNT peripheral in polled mode (`pcnt_unit_get_count` every 100 ms), with a
1 µs hardware glitch filter.

Pin-change attach is via `attachInterrupt(digitalPinToInterrupt(pin), handler, CHANGE)`
in `RCInput::begin()` (`hal/RCInput.h:39, 46`). No critical-section / `portENTER_CRITICAL`
wrapping on reads of the volatile shared struct.

### 2.3 Queues, semaphores, watchdog

| Object | Where | Depth / type | Producer | Consumer |
|---|---|---|---|---|
| `CommandQueue::_queue` | `system/CommandQueue.cpp:64` | xQueueCreate depth=16, item=OTPacket | Core 0 (web, DI handler), main loop helpers | Core 1 main loop drain at start of tick |
| `SessionLogger::_rowQueue` | `system/SessionLogger.cpp:76` | xQueueCreate depth=20, item=SessionRow | Core 1 loop `tick()` (sample sensor row) | Core 0 web task `drainQueue()` (write CSV) |
| `FlightRecorder::_mutex` | `system/FlightRecorder.cpp:27` | binary mutex | n/a | wraps every append, eviction, web read |
| `Watchdog` TWDT | `system/Watchdog.h:9-18` | `esp_task_wdt_init(5 s, panic=true)`, `esp_task_wdt_add(nullptr)` | n/a | Fed only from Core 1 `loop()` via `Watchdog::feed()`. Idle tasks not subscribed (see `-DCONFIG_ASYNC_TCP_USE_WDT=0` in `platformio.ini`) |

No critical sections, no atomics, no other mutexes anywhere in `EngineData`, `Config`,
or `HardwareConfig`. Cross-core coordination is implicit (32-bit aligned reads on Xtensa
are single-cycle).

---

## 3. Shared state surface

### 3.1 `EngineData` singleton (`engine/EngineData.h`)

Single global instance via `EngineData::instance()` (`EngineData.cpp:3-5`). No mutex.
Writers/readers are cross-core. Fields grouped:

- **Sensor readings** (`EngineData.h:39-57`): `n1Rpm`, `n2Rpm`, `tot`, `tit`, `oilPressure`, `oilTemp`, `fuelPressure`, `fuelFlow`, `p1`, `p2`, `battVoltage`, `torque`, raw ADC values.
- **Health flags** (`:60-69`): `n1Healthy`, `n2Healthy`, `totHealthy`, `titHealthy`, `oilHealthy`, `oilTempHealthy`, `fuelPressHealthy`, `flameDetected`. Drives whether SafetyMonitor checks run.
- **Demands** (`:72-86`): `throttleDemand` (0..1), `oilPumpPct`, `oilTargetBar`, `starterDemand`, `starterEnabled`, `propPitchDemand`, `fuelPump2Demand`, `glowPlugDemand`, `igniterOn`, `igniter2On`, `fuelSolOpen`, `bleedValveOpen`, `airstarterOpen`, `coolFanOn`, `oilScavengeOn`, `abPumpDemand`, `abSolOpen`, `abFuelOffset`.
- **AB state** (`:98-107`): `abMode` (ABMode enum), `abTriggerActive`, `abArmSwitchOn`, `abInputRaw`.
- **Mode / flags** (`:110-126`): `mode` (SysMode), `flameMonitorActive`, `relightArmed`, `relightAttempts`, `manualRelightActive`, `oilFailsafeActive`, `extraCooldownActive`, `standbyOilFeedActive`, `starterAssistActive`, `devMode`, `benchMode`, `skipSafetyChecks`, `limpMode`, `configVersionMismatch`.
- **Strings** (`:130-145`): `lastEvent[64]`, `faultDescription[160]`, `currentBlock[24]`, `seqIssues[N]`, etc. All `strncpy`d.
- **DI state** (`:148-152`): `diState[MAX_DI]`.
- **Timers** (`:176-180`): `extraCooldownUntilMs` (unsigned long).
- **Sequence telemetry** (`:158-170`): `seqBlockIdx`, `seqBlockTotal`, `seqIssueCount`, `seqHasErrors`.
- **Peaks** (`:206-214`): `maxN1`, `maxN2`, `maxTot`, `maxTit`, `maxP1`, `maxP2`, `maxOilTemp`, `maxBattVoltage`, `maxFuelPressure`.
- **Persistent counters** (`:220-223`): `bootCount`, `runCount`, `uptimeMs`, `totalRunSeconds` (in Config).

64-bit fields: none in EngineData. Timers are `unsigned long` (32-bit on ESP32 / IDF
LP64 model). Wraparound at ~49 days, relevant for `millis()` math throughout.

### 3.2 `Config` (`system/Config.{h,cpp}`)

LittleFS-backed (`/ecu_config.json`, with `/config.json` legacy fallback at
`Config.cpp:225-330`). Static members organised by section:

- Engine limits: `rpmLimit`, `totLimit`, `titLimit`, `oilMinBar`, `oilTempLimit`, `fuelPressMin`, `battVoltMin`, `hotStartTotThreshold`, `totRiseRateLimit`.
- Idle / governor: `idleTargetRpm`, `idleUseN2`, `idleMinPct`, `idleMaxPct`, `idleRampUpMs`, `idleRampDownMs`, `idleKi`, `governorTargetRpm`, `governorKp`, `governorBandRpm`, `usePropPitch`, `pitchRampSec`.
- Throttle: `throttleMinRaw`, `throttleMaxRaw`, `throttleExpo`, `throttleIdleMinPct`, `rampUpMs`, `rampDownMs`, `limpMaxThrottlePct`.
- Oil: `oilUseThrottleMap`, `oilMapMin`, `oilMapMax`, `oilKp`, `oilDeadband`, `oilFailsafePct`, `oilFailsafeDelayMs`, `standbyOilRpmLimit`, `standbyOilFeedPct`.
- Calibration polynomials / linear maps: oil pressure cubic (`oilPolyA..D`, `oilPolyXMin/Max`), P1/P2/fuelPress linear two-point, fuel flow pulse rate.
- AB: `abThrottleThreshold`, `abMainFuelOffsetPct`, `abPumpFollowThrottle`, `abPumpMin/MaxPct`, `abTorchTotLimit`.
- Relight: `relightEnabled`, `relightMinRpm`, `relightTimeoutMs`, `flameoutShutdownMs`.
- Tool timers: `toolFuelPrimeMs`, `toolOilPrimeMs`, `toolIgnTestMs`, `toolStartTestMs`, `toolFuelSolTestMs`.
- Cooldown skip: `cooldownSkipHoldMs`.
- Session log: `sessionLogMask`, `sessionLogIntervalMs`.
- Rules array (`Config::rules[8]`, evaluated by RulesEngine).
- Profile ID: `profileId`.

Persistence (`Config.cpp`):
- `load()` calls `_applyDefaults()`, then tries `/ecu_config.json`, falls back to `/config.json`.
- `save()` writes to `/ecu_config.json.tmp` then atomic-renames.
- No CRC, no HMAC, no version-migration steps (only `configVersionMismatch` UI flag).
- `requestSave()` sets `_savePending` flag; Core 0 (web task) calls `flushPendingSave()` to do the LittleFS I/O — keeps Core 1 free of blocking flash writes.
- `isLocked()` returns true in STARTUP/RUNNING/SHUTDOWN; web `PATCH /api/config` rejected unless `OT_DEV_MODE` build.

### 3.3 `HardwareConfig` (`system/HardwareConfig.{h,cpp}`)

LittleFS JSON (`/ecu_config.json` "hardware" section; legacy `/hardware.json`). All
~150 static members: pins, feature `has*` flags, actuator type selectors, sequence
block name arrays (`startupSeq`, `shutdownSeq`, `abSeq`, `abShutSeq`), DI channel
configs (role, debounce, faultMsg, faultCode, activeModes), profile_id, baud, sensor
SPI/CS pins, throttle/oil ADC channel.

No bounds checking on load. Invalid pin numbers (e.g. >48 on S3, ADC2 pin while WiFi
is up) accepted as-is and silently fail at driver init. `save()` uses same temp+rename
pattern. No CRC. `applyDefaults()` resets to `hardware_profile.h` compile-time values.

---

## 4. Calibration surface

| Map | Storage | Where consumed | Notes |
|---|---|---|---|
| Throttle ADC linear | `Config::throttleMinRaw / MaxRaw / Expo` | `Hardware::updateSensors` reads pot/RC | Two-point linear, with cubic expo curve |
| Oil pressure | `Config::oilPolyA/B/C/D + XMin/Max` (or legacy 2-pt) | `AnalogPolySensor` in `Hardware.h` | Cubic polynomial, no clamp on coefficients |
| Oil throttle-mapped target | `oilUseThrottleMap`, `oilMapMin`, `oilMapMax` | `Hardware.h:1183-1189` (computed each tick in RUNNING) | Linear, not a lookup table |
| P1/P2 pressure | `pX_rawMin/Max + valMax` | `AnalogLinearSensor` | Two-point linear |
| Fuel pressure | `fuelPress_rawMin/Max + valMax` | `AnalogLinearSensor` | Two-point linear |
| Fuel flow | `fuelFlowPulsesPerLitre` (pulse), or two-point linear (analog) | `Hardware::updateSensors` | Switched by `HardwareConfig::fuelFlowType` |
| Battery voltage | divider scale | `AnalogLinearSensor` | Two-point linear |
| Torque | offset + scale (Nm) | `AnalogLinearSensor` | Two-point linear |
| NTC oil temp | Steinhart-Hart B-parameter | `NTCSensor` | Configurable divider, no clamp |
| Rules engine | `Config::rules[0..7]` (sensor enum, op, threshold, actuator) | `RulesEngine::evaluate` | Evaluated every tick in RUNNING+STARTUP+SHUTDOWN |
| Idle/governor PI | `idleKi`, `governorKp`, `governorBandRpm`, `pitchRampSec` | DynamicIdle, PowerTurbineGovernor | Live-tunable; integrator windup limited inside controller |

---

## 5. Main loop ordering (Core 1, `main.cpp:1618-1689`)

```
Watchdog::feed()
checkStopSwitch()                 — STOP latch
checkStartSwitch()                — START edge + manual-relight hold
CommandQueue::drain(handleCommand)— processes web/DI-pushed OTPackets
Hardware::updateSensors()         — polls every sensor → EngineData
RCInput::tick()                   — converts ISR-captured PWM into rcIdle/rcThrottle
g_safety.check()                  — SafetyMonitor: 12 checks, may call enterShutdown/enterFaultShutdown
g_sequencer.tick()                — main start/shutdown block runner
g_abSequencer.tick()              — AB block runner
Hardware::runControllers()        — OilLoop / ThrottleSlew / DynamicIdle / Governor
[limp-mode throttle cap]          — applied after controllers
checkToolTimers()                 — STANDBY-only tool expiries
checkExtraCooldown()              — STANDBY cooldown timer
checkRelight()                    — keeps igniter on while relight criteria hold
checkABTrigger()                  — AB state machine evaluation
checkStarterAssist()              — starter PWM hold in RUNNING (hysteresis)
checkStandbyOilFeed()             — windmilling oil-feed in STANDBY
checkGeneralDI()                  — debounced DI polling, role dispatch
buzzerTick()                      — passive piezo FSM
checkCooldownSkip()               — START+STOP held in SHUTDOWN aborts cooldown
RulesEngine::evaluate()           — user-defined rules (run LAST in control chain)
Hardware::updateActuators()       — writes EngineData demands to PWM/relays, igniter dwell, AB offset
FlightRecorder::tick()            — periodic SNAP records
SessionLogger::tick()             — sample → row queue
ClusterSerial::tick()             — D:/S: outbound frames
g_mavlink.tick()                  — HEARTBEAT/NAMED_VALUE_FLOAT rate-paced
Hardware::tickStatusLED()         — blink FSM
[peak update]                     — health-gated max{N1,N2,TOT,TIT,P1,P2,oilTemp,battV,fuelPress}
edp.uptimeMs = millis()
```

Loop is uncapped (no `vTaskDelay`); runs as fast as it can. Web task gets 5 ms (STANDBY)
or 20 ms (engine active) sleeps.

---

## 6. Subsystem inventory for subagent dispatch

The classical piston-ECU roster (crank/cam, ignition advance, injector PW) does not
apply. Adapted roster for OpenTurbine:

| Subagent | Code in scope |
|---|---|
| **sequencer-state** | `engine/sequencer/SequenceEngine.h`, `IBlock.h`, all 30+ blocks under `engine/sequencer/blocks/`, AB state machine in `main.cpp:651-843`, mode transitions `main.cpp:924-1097` |
| **sensor-input** | `hal/sensors/*` (PCNTRpmSensor, MAX6675/31855/31856, NTC, DS18B20, AnalogPoly, AnalogLinear, AnalogThreshold), `hal/RCInput.h`, sensor-read path in `Hardware::updateSensors` |
| **actuator-output** | `hal/actuators/*`, all `g_act*` globals + selection pointers in `Hardware.h`, `Hardware::updateActuators`, `Hardware::allOff`, igniter dwell/coil-charge logic, AB pump scaling |
| **controllers** | `engine/controllers/{ThrottleSlew,DynamicIdle,OilPressureLoop,PowerTurbineGovernor,IController}.h`, `Hardware::initControllers`, `Hardware::runControllers`, limp-mode cap |
| **limits-protection** | `engine/SafetyMonitor.h`, `system/RulesEngine.h`, relight FSM in `main.cpp:476-505`, `checkStarterAssist`, `checkStandbyOilFeed`, `checkExtraCooldown`, `checkCooldownSkip`, DI fault role |
| **calibration-storage** | `system/Config.{h,cpp}`, `system/HardwareConfig.{h,cpp}`, `_savePending` deferred-write path, `applyDefaults`, partition layout |
| **comms-protocols** | `system/ClusterSerial.{h,cpp}`, `system/MAVLinkOutput.h`, `system/CommandQueue.{h,cpp}` |
| **wireless-web** | `system/web/WebServer.{h,cpp}` (HTTP/WS/OTA/captive portal), AP setup in `PlatformInit.h` + WebServer, mDNS, `Update` flow, JSON request handling |
| **rtos-architecture** | `webTask` creation, queue depths, mutex usage, watchdog config, IRAM placement, RCInput ISR safety, cross-core EngineData access pattern, `loop()` body ordering |
| **boot-init** | `setup()` in `main.cpp:1500-1616`, `PlatformInit.h`, GPIO default states before driver attach, sequence build/validation, profile mismatch handling, `partitions.csv` |
| **persistence-logging** | `FlightRecorder.{h,cpp}`, `SessionLogger.{h,cpp}` (deferred-write Core 0, LittleFS wear, eviction policy) |

`ignition-fuel` is folded into **actuator-output** (fuel solenoid, igniter dwell,
fuel pump 2) and **sequencer-state** (FuelOpen, FuelPulse, FlameConfirm, FuelPumpRamp,
FuelPumpIdle, ABIgnite torch).

---

## 7. Notable defaults and build flags

From `platformio.ini`:
- `-DCONFIG_ASYNC_TCP_USE_WDT=0` — AsyncTCP would otherwise subscribe itself to TWDT
  and block on `portMAX_DELAY`, causing periodic WDT panics.
- `-DCONFIG_ASYNC_TCP_QUEUE_SIZE=128` — default is 32, raised to absorb captive-portal
  HTTP+DNS bursts. Implication: more RAM pressure on async_tcp task.
- `-DCORE_DEBUG_LEVEL=0` — no library-level logging in release.
- `-mtext-section-literals` — keep ISR literal pools adjacent (Xtensa `l32r` reloc).
- `OT_DEV_MODE` is an opt-in build flag that enables config edits during RUNNING
  (`setup()` line 1533-1536); the comment says "NEVER ship this build".

Partition: 4 MB flash, dual 1.5 MB OTA slots, 56 KB spiffs region. `nvs` is reserved
but most config lives in LittleFS JSON, not NVS.

---

## 8. External input surfaces

| Surface | Where parsed | Notes |
|---|---|---|
| HTTP/WS web requests | `system/web/WebServer.cpp` | Shared 8 KB RX buffer for POST/PATCH bodies; no auth on any endpoint |
| OTA upload | `WebServer.cpp:817-859` (`/update`) | STANDBY-only gate; `Update.write()` streamed; no size limit declared |
| WiFi credentials | `WebServer.cpp` AP setup | AP password is optional; SSID = profile_id (broadcasts profile name) |
| LittleFS config files | `Config::load`, `HardwareConfig::load` | JSON; no CRC; corrupt → parse error → defaults |
| Cluster serial RX | none | TX-only |
| MAVLink RX | none | TX-only |
| GPIO digital inputs | DI channels (`main.cpp:545-642`), start/stop switches | Debounce, configurable activeH/pullup; role dispatch into FAULT/ESTOP path |
| RC PWM | `hal/RCInput.h` ISRs | Pulse-width range and freshness checked in tick |
| ADC sensor pins | `Hardware::updateSensors` | Calibration applied, no plausibility / range clamp on raw values |
| SPI thermocouple chips | MAX6675/31855/31856 drivers | Open-circuit / fault flags consumed into `*Healthy` |

---

## 9. Pause for confirmation

Recon is complete. Proposed subagent dispatch order (each writes
`.audit/<subsystem>/bugs.md`, then I aggregate into BUGS.md and FIXES.md):

1. **sequencer-state**  (largest surface, safety-critical state)
2. **limits-protection**  (SafetyMonitor + relight + DI fault path)
3. **actuator-output**  (Hardware.h updateActuators, igniter dwell, AB fuel offset)
4. **controllers**  (ThrottleSlew, DynamicIdle, OilLoop, Governor — sensor-loss behaviour)
5. **sensor-input**  (PCNT/SPI/ADC drivers + plausibility)
6. **rtos-architecture**  (cross-core, ISR, queue, watchdog)
7. **calibration-storage**  (Config/HardwareConfig load/save/migration)
8. **wireless-web**  (HTTP/WS/OTA/captive-portal parsers)
9. **comms-protocols**  (ClusterSerial, MAVLink, CommandQueue)
10. **boot-init**  (setup ordering, GPIO defaults)
11. **persistence-logging**  (FlightRecorder, SessionLogger)

Each subagent gets the bug taxonomy, finding template, and a scoped path list.

Ready to dispatch on your go.
