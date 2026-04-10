# Changelog

All notable changes to OpenTurbine are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2026-03-20

First public release. Complete, running firmware.

### Core engine
- Block-based startup sequencer: OilPrime → StarterSpin → PreIgnSpark → FuelOpen → FlameConfirm → PostIgnDwell → Spool → SafetyHold
- Block-based shutdown sequencer: ImmediateCut → RPMDrop → CooldownSpin → FinalStop
- Safety monitor: overspeed, overtemp, low oil, flameout — independently configurable per build
- RPM sensor health tracking: saturation, jump, zero-stuck, and glitch faults
- Relight support: automatic ignition retry on flameout if N1 is above minimum
- Limp mode: throttle cap on partial sensor failure
- Standby oil feed: windmill protection for windmilling engines in STANDBY
- Cooldown skip: hold both buttons in SHUTDOWN to bypass cooldown if needed
- Starter assist: post-spool starter engagement to help engine accelerate to idle

### Controllers
- OilPressureLoop: P-controller on oil pressure with throttle-mapped target (fixed or linear interpolation from idle to full throttle), open-loop failsafe on sensor fault, deadband to prevent hunting
- ThrottleSlew: configurable up/down ramp rates, overspeed and overtemp safety pullback
- DynamicIdle: closed-loop idle RPM hold with asymmetric ramp rates, deadband, N1 or N2 source selectable

### Sensors (HAL)
- PCNTRpmSensor: ESP32 hardware PCNT pulse counter for N1/N2 RPM
- AnalogSensor: three variants — polynomial (oil pressure), linear, threshold (flame detection)
- MAX6675TempSensor: SPI MAX6675 thermocouple with ring-buffer averaging and open-circuit detection
- MAX31855TempSensor: SPI MAX31855 thermocouple (direct SPI bit-bang, no library dependency)
- RCInput: interrupt-driven RC PWM decoder for idle pot and throttle position (replaces ADC on the same GPIO)
- MockSensor: scripted ramp values for DEV_MODE bench testing

### Actuators (HAL)
- ServoActuator: standard 50 Hz servo PWM 1000–2000 µs (throttle ESC, starter ESC)
- LEDCActuator: high-frequency ESP32 LEDC PWM (oil pump, cooling fans)
- RelayActuator: digital relay/MOSFET driver, active-high or active-low
- MockActuator: logs all calls for DEV_MODE bench testing

### Configuration system
- JSON config (20 sections, 80+ parameters) stored on LittleFS
- Profile ID safety check at boot — firmware and config must agree on `profile_id`, or all engine operations are blocked
- Config versioning with graceful fallback for new fields
- Config locked during STARTUP / RUNNING / SHUTDOWN; DEV_MODE bypasses locking
- Full config download/upload via web UI, PATCH endpoint for calibration wizard partial saves
- OTA firmware upgrade via `POST /update` (dual-partition, reboots into new image)

### Web interface (LittleFS, 7 static files, no build step)
- **Dashboard**: live N1, N2, TOT, oil pressure, flame, mode, sensor health, Start/Stop
- **Calibration**: guided wizards for throttle (min/max ADC capture), oil pressure (multi-point polynomial), flame threshold (automated noise-floor capture)
- **Config**: full parameter editor grouped by section, lock indicator, download/upload config.json
- **Log**: per-run summary cards (peak N1, peak TOT, duration, outcome), expandable event detail, JSON/CSV download
- **Tools**: diagnostic actuator tests (fuel prime, oil prime, ignition test, starter test, fuel solenoid test), all disabled outside STANDBY
- WebSocket telemetry push (configurable rate, default 200 ms)
- mDNS: accessible as `http://ot.local` in addition to `http://192.168.4.1`

### Data logging
- FlightRecorder: persistent ring-buffer event log on LittleFS (BOOT, START_ATTEMPT, block transitions, RUNNING_ENTRY, FAULT, ABORT, CONFIG_CHANGE, RELIGHT_ATTEMPT)
- SessionLogger: per-run CSV stream with configurable channel mask (N1, N2, TOT, oil, P1, P2, throttle, mode)
- ClusterSerial: JetEcu-compatible serial telemetry protocol for external instrument cluster displays

### Platform
- ESP32 classic, 4 MB flash, dual OTA partition table
- Core 1: ECU control loop (sensors → safety → sequencer → controllers → actuators → watchdog)
- Core 0: WiFi + AsyncWebServer + WebSocket push
- Hardware watchdog (5 s timeout)
- DEV_MODE for bench testing with mock sensors/actuators, live config, disabled safety locks

---

## Roadmap

Items under consideration for future releases — not committed:

- **Station mode Wi-Fi**: join an existing network instead of creating an AP
- **STM32 platform support**: platform abstraction layer is already MCU-agnostic
- **PT100/PT1000 sensor**: additional temperature sensor driver (ISensor drop-in)
- **Fuel flow sensor**: linear ADC driver + mass flow calculation
- **Afterburner block**: RUNNING-mode solenoid command with FlightRecorder logging
- **Air-starter block**: StarterSpin variant using pneumatic valve instead of ESC
- **Config profiles**: store and switch between multiple named config sets on LittleFS
- **MQTT telemetry**: push EngineData to a broker when in station mode
