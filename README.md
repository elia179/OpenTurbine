# OpenTurbine

**Universal turbine engine ECU firmware for ESP32.**

OpenTurbine is an open-source engine control unit for small jet turbines. It runs on a standard ESP32 (or ESP32-S3) development board and provides a complete control loop — startup sequencing, oil pump control, safety shutdowns, afterburner management, and a Wi-Fi web interface — with zero code changes between different engine builds. A compile-time profile provides safe factory defaults; the complete per-engine hardware and settings file is then tuned at runtime via the web UI.

For a practical first-setup walkthrough, use [`docs/BETA_USER_GUIDE.md`](docs/BETA_USER_GUIDE.md).

---

## Features

### Engine control
- **Block-based startup/shutdown sequencer** — each stage (oil prime, glow preheat, starter spin, pre-ignition, fuel open, flame confirm, temp confirm, spool, safety hold, cooldown…) is an independent, swappable, runtime-configurable block
- **Closed-loop oil pressure control** — P-controller with throttle-mapped target (idle → full throttle interpolation), open-loop failsafe on sensor fault, configurable deadband
- **Dynamic idle hold** — optional RPM-feedback feature with asymmetric ramp, PI integral term, selectable N1 or N2 source
- **Throttle slew limiting** — configurable up/down ramp rates with safety pullback near RPM/selected-EGT limits
- **Power turbine governor** — closed-loop N2 RPM control via propeller pitch servo for turboprop builds
- **Afterburner state machine** — full AB ignition/shutdown sequencer (ABCheckReady → ABIgnite → ABFlameConfirm → ABStabilize) with torch mode, fuel offset, pump-follows-throttle demand, independent igniter
- **Automation rules engine** — up to 8 user-defined threshold rules (sensor op value → actuator demand) evaluated every loop tick
- **Glow plug support** — current-sensor-based saturation detection with dwell/rest state machine
- **Bleed valve control** — compressor bleed valve commands for surge prevention

### Safety monitor
- Overspeed, overtemp (TOT and TIT), low oil pressure, oil-near-zero
- EGT rate-of-rise — triggers before temperature limit is reached
- Hot start detection — aborts startup if the selected EGT source is still too high
- Flameout detection with configurable hold time; automatic relight is available when N1 windmill feedback is fitted
- Optional compressor surge detection via N1 RPM variance analysis
- Fuel pressure low (in RUNNING only)
- Battery / bus undervoltage
- General-purpose digital input fault and e-stop channels
- Each check independently enabled/disabled in hardware config; all disabled in bench mode

### Sensors (HAL)
- **PCNTRpmSensor** — ESP32 hardware PCNT pulse counter; tracks RPM jump, saturation, zero-stuck, and zero-glitch faults
- **MAX6675TempSensor** — SPI thermocouple with ring-buffer averaging and open-circuit detection
- **MAX31855TempSensor** — direct SPI bit-bang, no library; K-type, fault detection
- **MAX31856TempSensor** — direct SPI bit-bang; all thermocouple types (B E J K N R S T), 0.0078 °C resolution
- **DS18B20TempSensor** — 1-Wire async digital thermometer (non-blocking, placement-new, 9–12 bit)
- **NTCSensor** — NTC thermistor via Steinhart-Hart B-parameter equation, configurable divider
- **AnalogPolySensor** — cubic polynomial (oil pressure), linear, and threshold (flame detection) variants
- **RCInput** — interrupt-driven RC PWM decoder for idle and throttle inputs (same GPIO as ADC, no pin change)
- **MockSensor** — development stub for scripted sensor values

### Actuators (HAL)
- **ServoActuator** — 50 Hz servo PWM 1000–2000 µs (throttle ESC, starter ESC, prop pitch)
- **LEDCActuator** — high-frequency ESP32 LEDC PWM (oil pump, fuel pump 2, AB pump), invertible
- **RelayActuator** — digital relay/MOSFET driver, active-high or active-low
- **MockActuator** — development stub that logs actuator calls

### Connectivity
- **Wi-Fi captive portal** — connecting to the ECU AP serves the dashboard for common captive-portal probes; direct `192.168.4.1` or `ot.local` remains the reliable path on Windows/Chrome
- **mDNS** — accessible as `http://ot.local` on any mDNS-capable client
- **MAVLink v1 TX** — HEARTBEAT, NAMED_VALUE_FLOAT telemetry, STATUSTEXT fault alerts over UART
- **Cluster serial** — OpenTurbine Cluster (OTC) binary protocol with schema discovery, CRC-framed telemetry, status/events, and optional wired RX commands
- **OTA updates** — browser-based firmware upload plus separate web UI asset upload, so firmware and dashboard pages can both be updated without erasing the engine file or logs

OTC wire-format details are documented in [`docs/OTC_CLUSTER_PROTOCOL.md`](docs/OTC_CLUSTER_PROTOCOL.md).

### Data logging
- **FlightRecorder** — mutex-protected persistent ring-buffer event log (BOOT, START_ATTEMPT, block transitions, RUNNING_ENTRY, FAULT, ABORT, RELIGHT_ATTEMPT, SNAP sensor snapshots)
- **SessionLogger** — per-run CSV stream with configurable channel mask at configurable interval; deferred Core 0 write path; automatic oldest-session eviction when flash is low

### Developer / diagnostic
- **Sequence validator** — at boot, cross-checks each block against fitted hardware; flags errors (block START) and warnings, displayed in web UI
- **Dev mode / bench mode** — explicit operator-gated diagnostics for bench testing, including safety bypasses and timer-based sequence completion; never use with fuel or a live engine unless you fully understand the risk
- **Buzzer patterns** — passive piezo tones for mode transitions and fault
- **Status LED** — mode-driven blink pattern (1–4 blinks + rapid fault flash). Classic ESP32 uses plain GPIO on/off; ESP32-S3 can use the YD onboard NeoPixel/RGB LED on GPIO48 or a normal external GPIO LED.
- **Peak value tracking** — session maxima for N1, N2, TOT, TIT, oil temp, fuel pressure, battery
- **Run counter and engine-time accumulator** — persisted across power cycles

---

## Hardware requirements

### Minimum for a real running engine

| Component | Notes |
|---|---|
| ESP32 or ESP32-S3 dev board | 4 MB flash minimum. Classic 30-/38-pin modules or ESP32-S3 DevKitC |
| Hardware STOP / fuel-cut path | A physical stop switch or equivalent external cut-off is mandatory for real engine testing. The ECU stop input defaults to GPIO 15 active-low. |
| EGT sensor (TOT or TIT) | MAX6675, MAX31855, MAX31856, or DS18B20 |
| Main fuel pump / throttle output | Servo ESC, LEDC PWM, or on/off output as supported by the Hardware page |
| Oil pump output | Servo ESC, LEDC PWM, or on/off output |
| Igniter or external light-off system | Relay, MOSFET, direct inductive coil drive, glow plug, or another verified ignition method |
| Safe startup sequence | The factory sequence is timer based: oil pump on, timed delay, fuel to idle, ignition window, then running. Review it before live fuel. |

> All pin numbers, sensor types, and feature enables are set at runtime via the **Hardware** web page — no recompile needed after initial flash.

### Strongly recommended feedback

| Component | Why it matters |
|---|---|
| N1 RPM sensor | Optional. Enables overspeed, surge detection, dynamic idle, relight windmill checks, starter/spool verification, and RPM run logs |
| Oil pressure sensor | Enables closed-loop oil pressure, low-oil shutdown, and oil-prime confirmation |
| Flame sensor or reliable selected-EGT rise confirmation | Lets startup verify combustion instead of relying only on timed delays |
| Fuel pressure sensor | Enables low fuel-pressure shutdown in RUNNING |
| Battery / bus voltage sensor | Detects undervoltage before actuator outputs become unreliable |

### Optional — all enabled at runtime

| Component | Role |
|---|---|
| N2 RPM sensor | Power turbine / compressor 2 shaft speed |
| TOT / TIT sensors | Turbine outlet and inlet temperature monitoring |
| Oil pressure sensor | Analog ADC |
| Oil temperature sensor | DS18B20, NTC, or analog |
| Flame sensor | Analog threshold (photodiode / UV) |
| Fuel pressure sensor | Analog ADC |
| Fuel flow sensor | Frequency-based or analog |
| Battery / bus voltage sensor | Analog ADC voltage divider |
| P1 / P2 pressure sensors | Inlet and exhaust pressure |
| Fuel pump 2 | Independent auxiliary fuel pump; servo ESC, LEDC PWM, or on/off output |
| Oil scavenge pump | Additional drain pump |
| Glow plug | With optional current sensor |
| Bleed valve | Compressor bleed solenoid or servo |
| Afterburner solenoid, pump, igniter | Full AB support |
| Propeller pitch servo | Turboprop N2 governor |
| Cooling fan | Relay or LEDC |
| Air-starter solenoid | Pneumatic starter valve |
| DI channels (×4) | Configurable roles: fault, estop, ab_arm, ab_fire, inhibit_start, limp_mode |
| Start switch | Hardware start button |
| Buzzer | Passive piezo for mode/fault tones |
| Status LED | Classic ESP32: any output GPIO. ESP32-S3/YD board: GPIO48 NeoPixel by default, or select plain GPIO on/off for an external LED |
| MAVLink UART | Any UART TX pin |
| Cluster serial | OTC external display/device protocol; TX telemetry plus optional RX commands. Fit the port in Hardware and control runtime streaming in Config > Cluster |
| RC PWM inputs | Servo signal for throttle and/or idle |

> **ADC note:** Analog sensors must be on ADC1 pins (GPIO 32–39 on classic ESP32; GPIO 1–10 on ESP32-S3). ADC2 is unavailable while Wi-Fi is active.

---

## Quick start

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 or ESP32-S3 dev board with USB cable

### 2. Clone the repository

```bash
git clone https://github.com/EliasLaaj/OpenTurbine.git
cd OpenTurbine
```

### 3. Set your hardware profile

Open `hardware_profile.h` and set the compile-time profile ID and any pin defaults. All hardware settings can be changed at runtime via the web UI — `hardware_profile.h` is only the starting point:

```cpp
#define OT_PROFILE_ID   "my_turbine_v1"   // default ID used when creating a new engine file
#define OT_PROFILE_DESC "My turbine, rev 1"

// Pin defaults (all overridable at runtime via Hardware web page)
#define OT_STOP_PIN    15    // MANDATORY
#define OT_START_PIN   13
```

For **ESP32-S3** use the `esp32s3dev` environment. The PlatformIO environment defines the target explicitly, and the profile selects the correct ADC pin range.

### 4. Flash the filesystem and firmware

Build firmware, upload it, then upload the gzip-compressed web UI filesystem:

```bash
pio run -e esp32dev -t upload
pio run -e esp32dev -t uploadfs
```

For ESP32-S3, replace `esp32dev` with `esp32s3dev`.

If the board is already in boot mode (IO0 low), the `--before=no_reset` flag in `platformio.ini` skips the automatic reset handshake.

### 5. Connect and configure

1. Power on the ESP32
2. Connect to the Wi-Fi AP — **SSID = the engine file's `hardware.profile_id`** (`OT_PROFILE_ID` is used for a newly created file)
3. Open **`http://192.168.4.1`** or **`http://ot.local`** in a browser
4. Go to **Hardware** first: choose the engine preset, fitted sensors/actuators, GPIO pins, Wi-Fi power, and optional features
5. Save Hardware and let the ECU reboot so pins are initialized correctly
6. Go to **Config** and set temperature limits, oil targets, telemetry, timing, and any RPM/relight settings only if those sensors are fitted
7. Go to **Calibration** and follow the guided wizards for every fitted input
8. Go to **Sequence** to review and customise startup, shutdown, afterburner, and control-rule behavior
9. Download `ecu_config.json` from Tools as a backup before live testing

> **Engine file identity:** `ecu_config.json` is the complete engine file. Its `hardware.profile_id` and `settings.profile_id` must match each other; a crossed file blocks engine operations. `OT_PROFILE_ID` supplies the default when a new file is created.

---

## Web interface

### Dashboard
Live values for fitted sensors such as TOT, TIT, RPM, oil pressure, battery voltage, mode, and sensor health. Start (with confirmation dialog) and Stop buttons. Color-coded gauge bars showing proximity to safety limits. Sparkline trends. Startup sequence progress bar. Fault description card with plain-language "what to do" guidance. Peak values, run count, and hour meter. Dashboard and calibration update near 3 Hz while connected.

### Calibration
Pre-flight checklist. Guided step-by-step wizards:
- **Throttle** — live bar, capture ADC min/max at stick endpoints
- **Oil pressure** — multi-point capture with known reference pressures, polynomial or linear fit
- **Flame sensor** — automated noise-floor capture

### Config
All settings grouped by section. **Basic / Expert toggle** hides advanced parameters for new users. Config is locked during engine operation. Full `ecu_config.json` backup and restore lives on Tools so hardware and settings stay together.

### Hardware
Runtime hardware topology editor — GPIO pin assignments, sensor types, actuator types, safety enables, DI channel roles, AB configuration, MAVLink / cluster serial settings — **without recompiling**. Requires reboot to apply. Engine must be in STANDBY to save.

### Sequence
Runtime sequence editor — define startup, shutdown, AB ignition, and AB shutdown block order by name without recompiling. Sequence validator results shown inline (errors block START; warnings are advisory). Changes take effect at the next START without a reboot.

### Log
Per-run summary cards with peak N1, peak TOT/TIT, duration, and outcome. Expandable event detail with sensor snapshots. Session Data tab with CSV preview and channel selection. Download as JSON or CSV.

### Tools
Diagnostic actuator tests (STANDBY only, all auto-expire):
- Fuel prime, fuel solenoid test, oil prime, oil scavenge test
- Igniter test (×2), glow plug test, starter test, starter enable test
- Idle throttle test, fuel pump 2 test, AB solenoid test, AB pump test
- Cooling fan test, airstarter test, bleed valve test, prop pitch test
- Extra cooldown (manual timed standby cooldown using the Sequencer CooldownSpin actuator settings)
- Full engine-file backup/restore, factory reset, firmware OTA, and web UI asset upload

---

## Configuration reference

All parameters live in `ecu_config.json` on LittleFS, editable via the web Config page.

| Section | What it controls |
|---|---|
| `engine` | RPM limit, min RPM, TOT limit, EGT cooldown target, soft warning/pullback margin |
| `oil` | Startup / running pressure targets, throttle-mapped min/max, P-gain, deadband, failsafe duty |
| `oil_advanced` | Oil-near-zero fault threshold, overcurrent threshold |
| `sequence.startup` | Per-block timeouts (oil prime, pre-ignition, flame confirm, spool, safety hold…) |
| `sequence.shutdown` | RPM drop timeout, cooldown timeout, cooldown mode, cooldown pressure target |
| `throttle` | Slew ramp rates (up/down), idle % range, expo curve, limp mode cap |
| `dynamic_idle` | Target RPM, ramp rates, deadband, P/I gains, N1 vs N2 source |
| `governor` | N2 target RPM, P-gain, pitch slew limit, pitch range (turboprop) |
| `safety` | Check interval, selected EGT source, TIT limit, flameout source/hold time, EGT rise-rate limit |
| `relight` | Enable/disable auto-relight, minimum N1 RPM, relight timeout |
| `afterburner` | Ignition limits, torch settings, flame-confirm mode, pump command source, fuel offset |
| `rules` | Up to 8 automation rules (sensor, op, threshold, actuator, on_value, off_value) |
| `standby_oil` | Windmill protection - selected N1/N2/either shaft threshold plus oil pump duty in STANDBY |
| `starter_assist` | Duty % and disengage RPM for post-spool starter support |
| `rpm_health` | Jump fault threshold, zero-stuck tick count |
| `tools` | Diagnostic test durations (fuel prime, oil prime, igniter, starter…) |
| `telemetry` | WebSocket push rate, flight recorder snapshot interval, standby logging |
| `cluster` | Runtime cluster output enable and warning thresholds; physical pins live in Hardware |
| `rc_input` | RC PWM pulse failsafe timeout; endpoints are calibrated on the Calibration page |
| `misc` | Cooldown skip hold time, igniter-on-start, manual relight settings |
| `session_log` | Channel mask (N1, N2, TOT, TIT, oil, P1, P2, throttle, mode…), log interval ms |
| `calibration` | Throttle ADC range, flame threshold, oil polynomial coefficients, voltage divider scale |

---

## Architecture

```
hardware_profile.h              ← factory defaults (pins, profile ID)
    │
    ▼
src/
├── main.cpp                    ← setup() / loop(), mode state machine, command handler
├── Hardware.h                  ← sensor/actuator globals, applyConfig(), runControllers()
│
├── hal/
│   ├── sensors/                ← ISensor: PCNT RPM, ADC (poly/linear/threshold),
│   │                               MAX6675, MAX31855, MAX31856, DS18B20, NTC, RC PWM, Mock
│   └── actuators/              ← IActuator: Servo PWM, LEDC PWM, Relay, Mock
│
├── engine/
│   ├── EngineData.h            ← central volatile data bus (singleton, Core 0 read-safe)
│   ├── SafetyMonitor.h         ← fault detection → enterFaultShutdown()
│   ├── controllers/
│   │   ├── OilPressureLoop.h   ← P-controller, throttle-mapped target, failsafe
│   │   ├── ThrottleSlew.h      ← ramp limiter, safety pullback
│   │   ├── DynamicIdle.h       ← PI idle RPM hold
│   │   └── PowerTurbineGovernor.h ← N2 closed-loop pitch controller (turboprop)
│   └── sequencer/
│       ├── SequenceEngine.h    ← IBlock state machine, bench mode, callbacks
│       └── blocks/             ← OilPrime, GlowPreheat, StarterSpin, PreIgnSpark,
│                                   FuelOpen, FuelPulse, FlameConfirm, TempConfirm,
│                                   TimedDelay, FuelPumpIdle, ModifiedIdle, Spool,
│                                   SafetyHold, ImmediateCut, RPMDrop, CooldownSpin,
│                                   FinalStop, BleedOpen/Close, FuelPumpRamp,
│                                   FuelPump2Set, GovernorHold, WaitForInput,
│                                   WaitTOTCool, ThrottleSet, PreHeat,
│                                   ABCheckReady, ABIgnite, ABFlameConfirm, ABStabilize,
│                                   + simple ActuatorBlocks (IgniterOn/Off, OilPumpOn/Off…)
│
├── system/
│   ├── Config.h/.cpp           ← JSON load/save, profile ID check, PATCH merge, locking
│   ├── HardwareConfig.h/.cpp   ← runtime hardware topology, read-modify-write save
│   ├── FlightRecorder.h/.cpp   ← persistent ring-buffer event log (Core 0 eviction)
│   ├── SessionLogger.h/.cpp    ← per-run CSV stream (Core 0 write path via queue)
│   ├── RulesEngine.h           ← threshold → actuator automation rules
│   ├── MAVLinkOutput.h         ← MAVLink v1 TX (hand-crafted CRC-16/MCRF4XX)
│   ├── ClusterSerial.h/.cpp    ← OTC external display/device protocol
│   ├── CommandQueue.h/.cpp     ← FreeRTOS queue, Core 0 → Core 1 commands
│   ├── Watchdog.h              ← ESP32 TWDT, 5 s timeout
│   └── web/                    ← ESPAsyncWebServer, WebSocket push, REST API
│
└── platform/esp32/
    ├── PlatformInit.h          ← Serial, LittleFS, NVS boot counter, ADC attenuation
    └── StatusLED.h             ← mode-driven blink pattern state machine
```

**Threading model:**
- **Core 1** (Arduino loop): sensors → RC input → safety → sequencer → controllers → limp cap → tool timers → relight → AB trigger → starter assist → standby oil → DI polling → buzzer → rules engine → actuators → flight recorder → session logger → cluster → MAVLink → LED → peaks
- **Core 0** (FreeRTOS task): Wi-Fi + AsyncWebServer + WebSocket telemetry push + LittleFS writes (session log drain, flight recorder eviction, config flush)

`EngineData` fields are `volatile` — individual 32-bit reads on Xtensa ESP32 are atomic; Core 0 reads are safe without a mutex. Commands travel Core 0 → Core 1 only via `CommandQueue`.

---

## Build environments

| Environment | Target | Notes |
|---|---|---|
| `esp32dev` | Classic ESP32 (240 MHz, 4 MB) | ADC1: GPIO 32–39 |
| `esp32s3dev` | ESP32-S3 DevKitC N8 target (240 MHz, 8 MB) | ADC1: GPIO 1–10; GPIO 19/20 reserved for USB; YD onboard RGB status LED uses GPIO48 NeoPixel mode by default |

```bash
pio run -e esp32dev   -t upload      # firmware
pio run -e esp32dev   -t uploadfs    # LittleFS web assets
pio run -e esp32s3dev -t upload
pio run -e esp32s3dev -t uploadfs
```

---

## Build flags

| Flag | Effect |
|---|---|
| `OT_DEV_MODE` | Boots with runtime Dev Mode already enabled. This exposes bench-mode and safety-bypass diagnostics without requiring the UI toggle first. **Never include in a production build.** |

---

## Safety

> OpenTurbine is provided as-is for experimental and educational use. Jet turbines are high-energy machines capable of causing serious injury or death. The software STOP command and web UI are **supplementary controls only** — a **hardware STOP switch** wired directly to `stopPin` is mandatory and must be capable of cutting engine power independently of the microcontroller. Always follow local regulations and safety guidelines when operating turbine engines.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

---

## License

MIT — see [LICENSE](LICENSE) for details.

---

## Contributing

Pull requests welcome. Keep PRs focused — one feature or fix per PR.
- New sensor driver → implement `ISensor` in `hal/sensors/`
- New actuator driver → implement `IActuator` in `hal/actuators/`
- New sequence block → implement `IBlock` in `engine/sequencer/blocks/`, register in `_blockRegistry[]` in `main.cpp`
- New config parameter → add to `Config.h`, `_fromDoc()`, `_toDoc()`, and the web Config page
