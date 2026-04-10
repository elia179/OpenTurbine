# OpenTurbine

**Universal turbine engine ECU firmware for ESP32.**

OpenTurbine is an open-source engine control unit for small jet turbines. It runs on a standard ESP32 development board and provides a full control loop — startup sequencing, oil pressure regulation, dynamic idle hold, safety shutdowns, and a Wi-Fi web interface — with zero code changes between different engine builds. All hardware differences are declared in a single file.

---

## Features

- **Block-based startup/shutdown sequencer** — each stage (oil prime, starter spin, pre-ignition, flame confirm, spool, safety hold, cooldown…) is an independent, swappable block
- **Closed-loop oil pressure control** — P-controller with throttle-mapped target and open-loop failsafe
- **Dynamic idle hold** — asymmetric RPM ramp with optional PI integral term eliminates steady-state droop
- **Direct throttle input mapping** — physical throttle pot or RC stick drives fuel ESC demand in RUNNING mode
- **Throttle expo curve** — configurable exponential response softens stick sensitivity near idle
- **Throttle slew limiting** — configurable up/down ramp rates prevent compressor stall
- **Safety monitor** — overspeed, overtemp, low oil, flameout — each independently configurable
- **RPM sensor health tracking** — detects saturated readings, zero-stuck, and RPM jump faults
- **Flight recorder** — persistent event log with sensor snapshots, downloadable as JSON or CSV
- **Session logger** — per-run data stream (N1, N2, TOT, oil, throttle) at configurable rate
- **Wi-Fi web UI** — dashboard, calibration wizard, config editor, log viewer, diagnostic tools
- **Captive portal** — connecting to the ECU's WiFi AP automatically opens the dashboard in any browser
- **Engine type presets** — one-click preset limits and timing for common turbine types
- **Config backup & restore** — download/upload full config as JSON from the Tools page
- **Relight support** — automatic ignition retry on flameout if N1 is sufficient
- **Limp mode** — throttle cap on partial sensor failure
- **Instrument cluster output** — serial telemetry to compatible external displays
- **OTA-capable partition table** — dual firmware slots, 1.375 MB LittleFS for web assets and logs
- **Everything optional** — every sensor, actuator, controller, and safety check compiles to zero code when not declared

---

## Hardware requirements

### Minimum (mandatory)

| Component | Notes |
|---|---|
| ESP32 development board | Classic ESP32, 4 MB flash. Tested on 30-pin and 38-pin dev modules |
| STOP switch | Active-low, wired to `OT_STOP_PIN` (default GPIO 15) |
| N1 RPM sensor | Hall-effect pulse sensor → PCNT counter (GPIO 14 default) |
| EGT/TOT sensor | MAX6675 or MAX31855 thermocouple module (SPI) |
| Oil pressure sensor | Analog 0–3.3 V output, ADC1 pin (GPIO 34 default) |
| Flame/ignition sensor | Analog threshold (photodiode or UV sensor), ADC1 pin (GPIO 35 default) |
| Throttle ESC | Standard servo PWM, 1000–2000 µs |
| Starter motor ESC | Standard servo PWM, 1000–2000 µs |
| Oil pump | PWM-controlled (MOSFET + BLDC or brushed pump) |
| Fuel solenoid | Relay or logic-level MOSFET |
| Igniter | Relay, MOSFET, or direct inductive coil drive |
| Starter enable relay | Powers the starter ESC |

### Optional

| Component | Enable flag |
|---|---|
| N2 RPM sensor | `OT_HAS_N2_RPM` |
| Fuel flow sensor | `OT_HAS_FUEL_FLOW` |
| P1 / P2 pressure sensors | `OT_HAS_P1`, `OT_HAS_P2` |
| Throttle position sensor | `OT_HAS_THROTTLE_POS` |
| Idle adjust potentiometer | `OT_HAS_IDLE_POT` |
| Afterburner solenoid | `OT_HAS_AB_SOL` |
| Air-starter solenoid | `OT_HAS_AIRSTARTER_SOL` |
| Cooling fan relay | `OT_HAS_COOL_FAN` |
| Status LED | `OT_HAS_STATUS_LED` |
| Instrument cluster (serial) | `OT_HAS_CLUSTER_SERIAL` |

> **ADC note:** Oil and flame sensors must be on ADC1 pins (GPIO 32–39). ADC2 is unavailable while Wi-Fi is active.

---

## Quick start

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 dev board with USB cable

### 2. Clone the repository

```bash
git clone https://github.com/your-username/OpenTurbine.git
cd OpenTurbine
```

### 3. Configure your hardware

Open `hardware_profile.h` to set compile-time defaults (pin numbers, profile ID). After first boot, you can fine-tune all hardware settings via the **Hardware** web page without recompiling — changes are saved to `ecu_config.json` on the device.

Set your profile ID and pin assignments:

```cpp
#define OT_PROFILE_ID   "my_turbine_v1"   // must match config.json at runtime
#define OT_PROFILE_DESC "My turbine, rev 1"

#define OT_STOP_PIN    15    // active-low hardware stop — MANDATORY
#define OT_START_PIN   13

// Enable only the sensors you have wired:
#define OT_HAS_N1_RPM
#define OT_N1_RPM_PIN  14

#define OT_HAS_TOT
#define OT_TOT_CLK  5
#define OT_TOT_CS   18
#define OT_TOT_MISO 19

// ...and so on for actuators, controllers, safety sources
```

Missing mandatory items cause clear compile-time errors. Optional items that are commented out compile to zero code.

### 4. Flash the filesystem

Upload the web UI to LittleFS (only needed once, or after UI changes):

```bash
pio run --target uploadfs
```

### 5. Build and flash firmware

```bash
pio run --target upload
```

### 6. Connect and calibrate

1. Power on the ESP32
2. Connect to the Wi-Fi access point: **SSID = your `profile_id`** (default: `OpenTurbine`). No password by default — you can set one in the Hardware page (`wifi_password` field)
3. Open **`http://192.168.4.1`** in a browser — or **`http://ot.local`** on any mDNS-capable client (Mac, Linux, Windows 10+)
4. Go to **Calibration** and follow the guided wizards for throttle, oil sensor, and flame threshold
5. Go to **Config** and tune parameters for your engine (RPM limits, temperature limits, oil pressure targets, timing)
6. Download `config.json` as a backup

> **First boot:** If no `ecu_config.json` exists on the device, default values are generated automatically. The `profile_id` in `hardware_profile.h` must match the `profile_id` field in `ecu_config.json` — a mismatch locks all engine operations and shows a banner on the Dashboard with step-by-step instructions to fix it. Go to the Hardware page and verify/update the `profile_id` field, or download the config, edit the field, and re-upload it.

---

## Web interface

Connect to the `OpenTurbine` access point and open `http://192.168.4.1` (or `http://ot.local`).

### Dashboard
Live N1 RPM, TOT, oil pressure, mode, and sensor health. Start (with confirmation dialog) and Stop buttons. Color-coded gauge bars show proximity to safety limits. Sparkline trends for N1 and TOT. Startup sequence progress bar during STARTUP/SHUTDOWN. Fault description card with plain-language "what to do" guidance. Hour meter showing total run count and accumulated engine-on time. WebSocket-driven updates at ~200 ms.

### Calibration
Pre-flight checklist at the top (check off items before first run). Guided step-by-step wizards for each analog sensor:
- **Throttle** — live bar, capture min/max at stick endpoints
- **Oil pressure** — multi-point capture with known reference pressures
- **Flame sensor** — automated noise-floor capture (no manual input needed)

### Config
All parameters editable in the browser, grouped by section. **Basic/Expert toggle** hides advanced tuning parameters in Basic mode so new users only see the essential settings. Config is locked during engine operation (unless Dev Mode is enabled). Download/upload `ecu_config.json` for backup and transfer between devices.

### Hardware
Runtime hardware topology editor — configure GPIO pin assignments, sensor types, actuator types, sequence names, and safety feature enables **without recompiling**. Settings are stored in the `hardware` section of `ecu_config.json`.

> **Important:** Hardware changes require a reboot to take effect. The engine must be in STANDBY to save Hardware changes. Pin numbers must match your physical wiring.

> **Profile ID:** The `profile_id` field (also used as the WiFi AP SSID) must match the `OT_PROFILE_ID` defined in `hardware_profile.h`. A mismatch locks all engine operations and shows an error on the dashboard with instructions to fix it.

### Sequence
Runtime sequence editor — define the startup and shutdown block order by name without recompiling. Each block name must exist in the block registry (see `main.cpp`). Changes take effect at the next START command without requiring a reboot.

### Log
Per-run summary cards with peak N1, peak TOT, duration, and outcome. Expandable event detail with sensor snapshots. Session Data tab shows the in-browser CSV preview (last 50 rows) and lets you choose which channels to log. Download as JSON or CSV.

### Tools
Diagnostic actuator tests available in STANDBY:
- Fuel prime, oil prime, ignition test, starter test, fuel solenoid test
- Extra cooldown (runs oil pump/starter for additional cooling after shutdown)

---

## Configuration reference

Runtime parameters live in `config.json` (stored on LittleFS, editable via web UI).

Key sections:

| Section | What it controls |
|---|---|
| `engine` | RPM limit, min RPM, TOT limit, TOT cooldown target, safe margin |
| `oil` | Startup pressure, running pressure target, throttle-mapped min/max, controller gains, failsafe duty |
| `oil_advanced` | Zero-pressure fault threshold, P-controller deadband |
| `sequence.startup` | Per-block timeouts and RPM thresholds (oil arm, pre-ignition, flame confirm, spool) |
| `sequence.shutdown` | RPM drop timeout, cooldown timeout, cooldown hardware selection |
| `ignition` | Post-ignition dwell time before spool |
| `throttle` | Slew ramp rates (up/down), idle percentage range |
| `dynamic_idle` | Target RPM, ramp rates, deadband, N1 vs N2 source |
| `safety` | Check interval, flameout hold duration |
| `relight` | Enable/disable auto-relight on flameout, minimum RPM to attempt |
| `standby_oil` | Windmill protection — oil pump activation threshold and duty in STANDBY |
| `starter_assist` | Post-start starter assist duty % and disengage RPM |
| `limp_mode` | Throttle cap when limp mode is active |
| `rpm_health` | RPM jump fault threshold, zero-stuck tick count |
| `tools` | Diagnostic test durations (fuel prime, oil prime, ignition test, etc.) |
| `telemetry` | WebSocket push rate, flight recorder snapshot interval |
| `cluster` | Warning thresholds for external cluster display |
| `display` | Dashboard options (show/hide P1/P2 pressure sensors) |
| `rc_input` | RC PWM calibration (min/max µs, failsafe timeout) |
| `misc` | Cooldown skip hold time, igniter-on-start behaviour |
| `session_log` | Which channels to record in the per-run CSV (N1, N2, TOT, oil, P1, P2, throttle, mode) |
| `calibration` | Throttle ADC range, flame threshold, oil polynomial coefficients, P1/P2 zero offsets |

All fields are documented with descriptions and default values in the web Config page.

---

## Architecture overview

```
hardware_profile.h          ← compile-time hardware topology (only file you edit)
    │
    ▼
src/
├── main.cpp                ← setup() / loop() wiring only
├── Hardware.h              ← sensor + actuator instances, applyConfig(), runControllers()
│
├── hal/
│   ├── sensors/            ← ISensor implementations (PCNT RPM, ADC, MAX6675, MAX31855, mock)
│   └── actuators/          ← IActuator implementations (servo PWM, LEDC, relay, mock)
│
├── engine/
│   ├── EngineData.h        ← central volatile data bus (singleton, read by Core 0 safely)
│   ├── SafetyMonitor.h     ← fault detection → enterShutdown()
│   ├── controllers/        ← OilPressureLoop, ThrottleSlew, DynamicIdle
│   └── sequencer/
│       ├── SequenceEngine.h
│       └── blocks/         ← OilPrime, StarterSpin, PreIgnSpark, FuelOpen,
│                               FlameConfirm, PostIgnDwell, Spool, SafetyHold,
│                               ImmediateCut, RPMDrop, CooldownSpin, FinalStop
│
├── system/
│   ├── Config.h/.cpp       ← JSON load/save, profile ID check, config locking
│   ├── FlightRecorder.h/.cpp ← persistent event log to LittleFS
│   ├── SessionLogger.h/.cpp  ← per-run data stream
│   ├── ClusterSerial.h/.cpp  ← serial telemetry to external cluster
│   ├── CommandQueue.h/.cpp   ← thread-safe Core 0 → Core 1 command pipe
│   └── web/                  ← AsyncWebServer, WebSocket, REST endpoints
│
└── platform/esp32/         ← MCU-specific code (PCNT, LEDC, PlatformInit, StatusLED)
```

**Threading model:**
- Core 1 (Arduino loop): sensors → EngineData → safety → sequencer → controllers → actuators → watchdog
- Core 0 (FreeRTOS): Wi-Fi + AsyncWebServer + WebSocket telemetry push

`EngineData` fields are `volatile` — Core 0 reads are safe without mutex. Commands travel Core 0 → Core 1 only via `CommandQueue` (FreeRTOS queue, non-blocking on both ends).

For full architecture detail, see [`DESIGN_SPEC.md`](DESIGN_SPEC.md).

---

## Build flags

| Flag | Effect |
|---|---|
| `OT_DEV_MODE` | Allow config changes during engine operation, enable mock sensors/actuators, bypass safety locks. **Never ship with this enabled.** |

---

## Safety notes

> OpenTurbine is provided as-is for experimental and educational use. Jet turbines are high-energy machines capable of causing serious injury or death. The software STOP command and web UI are supplementary controls only — a **hardware STOP switch** wired directly to `OT_STOP_PIN` is mandatory. Always follow your local regulations and safety guidelines when operating turbine engines.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the full version history.

---

## License

MIT — see [LICENSE](LICENSE) for details.

---

## Contributing

Pull requests welcome. Please keep PRs focused — one feature or fix per PR. If you're adding a new sensor or actuator type, follow the `ISensor` / `IActuator` interface pattern in `hal/`. If you're adding a new sequence block, follow `IBlock` in `engine/sequencer/`.
