# Changelog

All notable changes to OpenTurbine are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Fixed
- Bumped firmware/UI version to 1.2.0 for beta-test builds.
- Restored Hardware page controls for the passive buzzer so fitted buzzer pins can be assigned, validated, and included in GPIO conflict checks.
- Replaced stale flashing instructions that referenced a missing helper script with the actual PlatformIO firmware and filesystem upload commands.
- Aligned the factory profile and setup guide with the standard N1-free setup; primary EGT safety can use TOT or TIT, while N1-dependent dynamic idle, overspeed, surge, and auto-relight remain optional features.
- Flight log run summaries now preserve TIT peaks and the Log page reads the real firmware event field names (`n1Rpm`, `totDegC`, `titDegC`, `oilBar`, `maxTot`, `maxTit`).
- Dashboard EGT approach warnings now follow the selected engine-safety EGT source instead of always treating TOT as primary.
- Extra Cooldown UI/docs now describe the actual Sequencer CooldownSpin actuator settings instead of fixed starter-plus-oil behavior.
- Documentation now calls out ESP32-S3 GPIO48 NeoPixel status LED mode separately from plain GPIO status LEDs, and platform notes no longer refer to old GPIO38/S3 auto-detect assumptions.

### Documentation
- Added a beta-readiness review plan covering supported setups, dependency gates, critical user flows, and required verification.
- Updated the README hardware/setup guide to reflect the unified `ecu_config.json` engine file, runtime hardware configuration, OTA web assets, and current ESP32-S3 target.

---

## [1.1.0] — 2026-05-24

Major feature release. Significant expansion of hardware support, safety system, and afterburner capability.

### New: Afterburner system
- Full afterburner state machine (Off → Arming → Igniting → Running → ShuttingDown → Fault)
- Configurable AB ignition sequence: ABCheckReady, ABIgnite, ABFlameConfirm, ABStabilize
- ABIgnite: torch mode (opens main fuel solenoid briefly to confirm TOT rise before committing fuel), separate igniter 2 channel, configurable retries
- AB fuel offset applied at actuator level — does not contaminate ThrottleSlew feedback
- AB pump demand follows throttle (lerp min→max) or fixed at max, configurable
- Trigger sources: manual (web UI), throttle threshold, dedicated switch pin, analog/RC input
- Rising-edge latch prevents rapid re-entry while trigger is held after Fault
- Arm-switch gate (optional): AB only fires when arm switch is also asserted
- ABCheckReady validates armed/throttle/TOT conditions before ignition sequence starts
- ABFlameConfirm: TOT-rise confirmation alternative to photodiode (for AB without flame sensor)
- Independent DI channel role `ab_fire` for external trigger
- `enterFaultShutdown()` synchronously stops AB sequence; AB actuators cut immediately

### New: Safety monitor checks
- **EGT rate-of-rise** (`TOT_RISE`) — triggers before temperature limit is reached; configurable °C/s
- **Hot start detection** (`HOT_START`) — aborts startup if TOT exceeds threshold; configurable per-engine
- **Compressor surge detection** (`SURGE`) — N1 RPM variance analysis over a 10-sample ring buffer
- **TIT overtemp** (`TIT_OVERTEMP`) — turbine inlet temperature limit, independently enabled
- **Oil temperature high** (`OIL_TEMP_HIGH`) — oil temp limit, independently enabled
- **Fuel pressure low** (`FUEL_PRESS_LOW`) — active in RUNNING only, independently enabled
- **Battery / bus undervoltage** (`BATT_LOW`) — configurable minimum voltage, independently enabled
- All safety checks now independently enabled/disabled via HardwareConfig flags
- Each fault provides a plain-language `faultDescription` with "what to do" guidance shown in the web UI

### New: Sensors
- **MAX31856TempSensor** — direct SPI bit-bang; supports all thermocouple types (B E J K N R S T); 19-bit resolution (0.0078 °C/LSB); continuous-conversion mode
- **DS18B20TempSensor** — async 1-Wire Dallas/Maxim thermometer; non-blocking requestTemperatures/read cycle; 9–12 bit resolution; filters 85 °C power-on reset value; placement-new (no heap)
- **NTCSensor** — NTC thermistor with Steinhart-Hart B-parameter equation; configurable R0, T0, beta, divider resistor

### New: Controllers
- **PowerTurbineGovernor** — closed-loop N2 RPM P-controller driving propeller pitch servo for turboprop/turboshaft builds; dt-scaled output; hard pitch slew cap; pitch-primary mode

### New: Blocks
- **GlowPreheat** — current-sensor-based glow plug saturation detection (dwell/rest state machine); skips immediately when no glow plug configured
- **BleedOpen / BleedClose** — compressor bleed valve commands for surge prevention
- **FuelPump2Set** — set secondary variable-speed fuel pump demand
- **FuelPumpRamp** — ramp fuel pump demand from current to target over configurable time
- **GovernorHold** — hold N2 governor at target until RPM is stable
- **FuelPulse** — pulsed fuel open/close cycle as alternative to continuous FuelOpen
- **WaitTOTCool** — block until TOT falls below configurable target
- **WaitForInput** — block until a configured DI channel activates (with timeout)
- **ThrottleSet** — set throttle demand to a specific value and hold
- **PreHeat** — configurable pre-ignition heating stage
- **ModifiedIdle** — post-startup idle at a lower RPM for one sequencer cycle
- **TempConfirm** — TOT-rise based flame confirmation (alternative to FlameConfirm for builds without a photodiode)
- **TimedDelay** — simple configurable delay block
- **ActuatorBlocks** — IgniterOn/Off, Igniter2On/Off, FuelSolClose, StarterEnOn/Off, StarterOff, OilPumpOn/Off, OilScavengeOn/Off, CoolFanOn/Off, AirstarterOn/Off, ABPumpOn/Off, ABIgnOn/Off, ABSolOpen/Close

### New: Hardware support
- **ESP32-S3** build environment (`esp32s3dev`) — correct ADC1 pin range (GPIO 1–10), USB pins reserved
- **Glow plug** with optional current-sense feedback
- **Oil scavenge pump** — second oil pump for scavenge/dry-sump systems
- **Fuel pump 2** — independent variable-speed secondary fuel pump
- **Air-starter solenoid** — pneumatic starter valve
- **Cooling fan** — relay or LEDC PWM
- **Buzzer** — passive piezo with patterns: rapid fault beep, startup chirp (×2), running beep, shutdown beep
- **MAVLink v1 output** — hand-crafted HEARTBEAT, NAMED_VALUE_FLOAT, STATUSTEXT over any UART TX; CRC-16/MCRF4XX (X25)
- **DI channels (×4)** — configurable role per channel: `fault`, `estop`, `ab_arm`, `ab_fire`, `limp_mode`, `inhibit_start`; debounced; rising/falling edge handling; boot-state seed prevents spurious triggers
- **Torque / shaft power** — optional analog torque sensor with shaft power calculation
- **Oil temperature** sensor support
- **TIT sensor** (turbine inlet temperature) — separate sensor channel from TOT
- **P1 / P2 pressure** sensors — configurable in hardware, displayed in dashboard
- **Battery voltage** — configurable divider scaling
- **Prop pitch servo** — for turboprop/turboshaft power turbine governor

### New: System
- **RulesEngine** — up to 8 user-defined automation rules (sensor, comparison operator, threshold → actuator demand); evaluated last in the control chain after all safety checks
- **Sequence validator** — runs at boot and after `buildSequences()`; checks block names (unknown names flagged as errors), required sensors per block, and config sanity (e.g. `idleUseN2=true` without N2 sensor); results stored in EngineData and shown in the Sequence web page
- **HardwareConfig** — full runtime hardware topology stored in `ecu_config.json hardware` section; covers pins, sensor types, safety enables, sequence names, DI config, AB config, MAVLink, cluster serial, buzzer, status LED; `save()` uses read-modify-write to preserve other config sections
- **Config automation rules section** — `rules[]` array in `ecu_config.json`
- **Config version mismatch detection** — warning shown in web UI when firmware default fields differ from saved config
- **Peak value tracking** — session maxima for N1, N2, TOT, TIT, P1, P2, oil temp, fuel pressure, battery voltage; health-gated; reset via web command
- **Run count and engine-time accumulator** — NVS-persisted; bench/dev runs excluded
- **Web UI assets gzip-compressed** — `data/` contains `.gz` versions; original sources in `data_src/`; significant LittleFS space savings; `tools/gzip_data.py` provided for regeneration
- **Relight attempt counter** — tracks attempts per run in EngineData; logged in FlightRecorder
- **Limp mode** — auto-engaged by SafetyMonitor when N1 sensor is lost; also togglable via DI channel or web command; throttle capped at `limpMaxThrottlePct`
- **Starter assist** — post-spool starter engagement in RUNNING; hysteresis to prevent chattering; disabled when RPM sensor unhealthy
- **Manual relight** — hold START button while RUNNING to force igniter on (configurable via `igniterOnStart`)
- **Extra cooldown tool** — operator-initiated post-shutdown cooldown spin from web UI; duration configurable; conflicts with actuator test tools
- **Standby oil feed** — windmill protection; oil pump runs at low duty in STANDBY when N1 above threshold (e.g. engine windmilling on the bench)
- **Cooldown skip** — hold START+STOP simultaneously for configurable time during SHUTDOWN to skip to STANDBY
- **Config `requestSave()`** — deferred LittleFS write from Core 1; Config saves handled by Core 0 to avoid stalling the ECU loop

### Bug fixes
- **`SessionLogger::_evictOldSessions()`** — `entry.name()` on LittleFS can return the full path (`/logs/session_N.csv`) rather than just the filename; `sscanf` failed silently, flash could fill up without eviction; fixed by stripping directory prefix with `strrchr`
- **`WebServer.cpp` PONG rescue** — when `canSend()` was false during a full-frame WebSocket event (counter=60), `_wsPendingFull` was never set, so the PONG rescue always sent a fast frame; dashboard labels and limits would not refresh until the next 30-second cycle; fixed by saving the full-frame flag across the deferred path
- **`Config.cpp` ArduinoJson v7 const** — `_fromDoc(const JsonDocument&)` iterated `rules` array as `JsonArray` / `JsonObject` — invalid from a const document in v7; changed to `JsonArrayConst` / `JsonObjectConst`
- **`MAX31856TempSensor.h`** — dead code: first assignment of `val` was immediately overwritten; removed
- **`PlatformInit.h`** — missing `#include "../../engine/EngineData.h"`; relied on transitive inclusion from `Hardware.h`; added direct include

---

## [1.0.0] — 2026-03-20

First public release. Complete, running firmware.

### Core engine
- Block-based startup sequencer: OilPrime → StarterSpin → PreIgnSpark → FuelOpen → FlameConfirm → Spool → SafetyHold
- Block-based shutdown sequencer: ImmediateCut → RPMDrop → CooldownSpin → FinalStop
- Safety monitor: overspeed, overtemp, low oil, flameout — independently configurable
- RPM sensor health tracking: saturation, jump, zero-stuck, glitch faults
- Relight support: automatic ignition retry on flameout if N1 is above minimum
- Limp mode: throttle cap on partial sensor failure
- Standby oil feed: windmill protection in STANDBY
- Cooldown skip: hold both buttons in SHUTDOWN to bypass cooldown
- Starter assist: post-spool starter engagement to help reach idle

### Controllers
- OilPressureLoop: P-controller, throttle-mapped target, open-loop failsafe, deadband
- ThrottleSlew: configurable ramp rates, overspeed and overtemp safety pullback
- DynamicIdle: closed-loop idle RPM hold with asymmetric ramp, deadband, N1 or N2 selectable

### Sensors
- PCNTRpmSensor: ESP32 hardware PCNT, health fault tracking
- AnalogSensor: polynomial (oil), linear, threshold (flame)
- MAX6675TempSensor: SPI thermocouple with ring-buffer averaging
- MAX31855TempSensor: direct SPI bit-bang, K-type
- RCInput: interrupt-driven RC PWM decoder
- MockSensor: scripted ramp for bench testing

### Actuators
- ServoActuator: 50 Hz servo PWM
- LEDCActuator: LEDC high-frequency PWM
- RelayActuator: digital relay/MOSFET, active-high or active-low
- MockActuator: logs all calls for bench testing

### System
- JSON config (20+ sections, 80+ parameters), profile ID safety check
- FlightRecorder: persistent ring-buffer event log on LittleFS
- SessionLogger: per-run CSV stream with configurable channel mask
- ClusterSerial: OTC framed external display/device telemetry protocol
- Web interface: Dashboard, Calibration, Config, Sequence, Log, Tools
- ESP32 classic, 4 MB flash, dual OTA partition, hardware watchdog
- Core 1: ECU control loop; Core 0: Wi-Fi + WebServer + WebSocket
