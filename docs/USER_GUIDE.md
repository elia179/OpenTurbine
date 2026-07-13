# OpenTurbine detailed user guide

OpenTurbine is an open-source ESP32 turbine engine controller with a built-in web interface. It is intended for experimental turbojets, APUs, generators, turboshafts, turboprops, and other small turbine installations—not only aircraft.

The project aims to support the variety found in hobby turbines instead of prescribing one engine layout. The normal interface keeps common setup simple and shows only controls relevant to the fitted hardware; Advanced views, configurable sequences, rules, sensor models, and actuator choices remain available for unusual installations. Suggestions are starting points to verify, not mandatory engine settings. OpenTurbine should require an action only when needed to prevent an immediate unsafe operation, destructive data loss, or an internally invalid configuration.

> **Experimental engine control software:** A turbine can cause fire, burns, overspeed failure, fuel spray, projectiles, hearing damage, and death. OpenTurbine does not know the safe limits of your engine. You must verify every limit, output direction, shutdown path, and sequence on a safe test stand before introducing fuel.

## Download and install

### Windows—the easy path

**[Download OpenTurbine Setup Tool](https://github.com/elia179/OpenTurbine/releases/latest/download/OpenTurbineSetupTool.exe)**

1. Connect an ESP32 or supported ESP32-S3 board by USB.
2. Open `OpenTurbineSetupTool.exe`.
3. Choose **Clean install / reinstall**. This USB path erases the selected board and is correct for a blank board or an intentional fresh installation on an older board. If the older board still works, first download its complete engine file from Tools; the clean-install path cannot recover erased settings.
4. Select the detected board and follow the instructions.
5. After installation, join the Wi-Fi network shown by the tool and open `http://192.168.4.1`.

For normal upgrades, choose **Update and keep my setup**. That Wi-Fi path backs up the engine settings and updates firmware and web pages without resetting the existing setup.

If the download link says **Not Found**, the first public release has not been published yet. Do not download an installer offered by an unrelated third party. Releases belong at [github.com/elia179/OpenTurbine/releases](https://github.com/elia179/OpenTurbine/releases).

### If Windows blocks the installer

The current installer may be unsigned or may not yet have Microsoft download reputation, so Microsoft Edge, Chrome, or SmartScreen may warn about a new or uncommon application. Windows 11 Smart App Control can block unsigned or untrusted apps more strictly.

Only continue when the file came from the official `elia179/OpenTurbine` release page. In SmartScreen choose **More info → Run anyway**. If a browser blocks the download, open its Downloads panel and choose to keep the file. Do not disable Windows security globally for OpenTurbine. A release may also include a `.sha256` file for checksum verification.

The setup tool can install the USB serial driver needed by common CP210x and WCH CH340/CH341/CH343 boards. It detects the connected USB bridge hardware ID and offers only the matching driver. After driver installation it rescans Windows and continues automatically when the matching COM port appears; reconnect or restart only if the tool explicitly asks.

### Linux, macOS, or manual PlatformIO installation

The graphical setup tool is currently Windows-only. Developers and users comfortable with PlatformIO can install manually:

```bash
git clone https://github.com/elia179/OpenTurbine.git
cd OpenTurbine
python tools/gzip_data.py

# Classic ESP32:
pio run -e esp32dev -t upload
pio run -e esp32dev -t uploadfs

# Or the supported ESP32-S3 N16R8 target:
pio run -e esp32s3dev -t upload
pio run -e esp32s3dev -t uploadfs
```

Use only the environment matching the board. The filesystem upload is required because it contains the web interface. Specify `--upload-port` when automatic port detection chooses incorrectly. A partition-table change requires both firmware and filesystem installation over USB.

## What you need

### To install and explore without running an engine

- Windows computer for the setup tool
- ESP32 development board with at least 4 MB flash, or the supported ESP32-S3 DevKitC-1 N16R8 target
- Data-capable USB cable
- A phone or computer with Wi-Fi and a browser

You can explore the web interface and configure hardware without connecting fuel, ignition, pumps, or a turbine.

### Before a fueled turbine test

The exact equipment depends on the engine. At minimum, a real installation needs:

- An ECU board and a stable power supply sized for every connected load
- A controllable main fuel output: pump/ESC, metering actuator, or equivalent
- A positive fuel shutoff method appropriate to the installation
- An ignition system
- A safe starting method: starter motor, air starter, or external air source
- Lubrication hardware required by the engine, including oil and scavenge pumps where applicable
- A physical emergency fuel/power stop independent of the browser
- Proper wiring protection, fusing, grounding, connectors, and flyback suppression
- A restrained outdoor test stand, exclusion zone, hearing/eye protection, and suitable fire suppression

OpenTurbine can execute a purely timed sequence without sensors, but that is **not a recommendation for a fueled test**. For a responsible first run, fit feedback that can detect the failures relevant to your engine. Usually this means:

- Shaft RPM for overspeed protection
- TOT/EGT or TIT for over-temperature and hot-start protection
- Oil pressure when the engine uses a pressure-fed lubrication system
- A combustion indication such as flame sensing, RPM behavior, or EGT behavior

Sensor type and location matter. A safe value at one station may be unsafe or meaningless at another. Use the engine manufacturer’s information, sensor datasheets, mechanical gauges, and controlled bench data.

## Electrical and wiring basics

OpenTurbine assigns pins from the Hardware page, so there is no single universal wiring diagram. The Hardware page’s pin list and conflict checker are authoritative for the selected ESP32 target. The rules below explain how the common connections work.

### Before connecting anything

1. Disconnect fuel, ignition energy, starter power, pumps, and other high-current supplies.
2. Power the ESP32 from a clean regulated supply within the board manufacturer’s limits.
3. Confirm every external signal voltage with a meter before connecting it to a GPIO.
4. Join logic grounds unless an interface is intentionally isolated.
5. Keep sensor wiring away from igniter, starter, pump, and motor wiring.
6. Fuse load supplies and use wire/connectors rated for starting and stall current.

ESP32 GPIO is **3.3 V logic and is not 5 V tolerant**. A sensor advertised as “0.5–4.5 V” cannot connect directly to an ADC input; use a correctly calculated divider or signal conditioner that keeps the ECU pin at or below 3.3 V under every fault and supply condition.

### ESP32 pin rules

| Target | Analog inputs | Important restrictions |
|---|---|---|
| Classic ESP32 | Use ADC1 GPIO 32–39 while Wi-Fi is active | GPIO 34–39 are input-only. GPIO 6–11 are normally connected to internal flash. Avoid boot-strapping pins unless the external circuit cannot disturb boot. ADC2 is not reliable while Wi-Fi is active. |
| Supported ESP32-S3 N16R8 | Use ADC1 GPIO 1–10 | GPIO 19/20 are native USB D−/D+. GPIO 22–25 are not implemented. GPIO 26–32 are normally flash/PSRAM connections. Verify the exact module before using any pin outside the Hardware page choices. |

Never copy a classic ESP32 pin number into an S3 installation without reselecting the target and pins in Hardware.

### Power and high-current outputs

An ESP32 pin is a logic signal, not a pump, starter, solenoid, relay-coil, glow-plug, or igniter power source.

```text
ESP32 output -> suitable gate/driver/ESC input -> actuator
ESP32 GND    -> driver/ESC signal ground
Load supply  -> fuse -> driver/ESC -> actuator
```

- Use a logic-level MOSFET driver, protected automotive driver, relay module with a valid 3.3 V input, or appropriate ESC.
- Fit flyback suppression across DC relay/solenoid coils unless the chosen driver already contains it.
- Keep motor and ignition current out of the ESP32 ground path. Use a deliberate grounding layout and adequate conductors.
- Verify active-high/active-low behavior with the load power disconnected first.
- Direct ignition-coil drive requires a proper current-limited switching stage. Never connect an ignition coil directly to the ESP32.

### Servo or ESC output

```text
ESP32 configured servo/ESC output -> signal
ESP32/ECU ground                  -> signal ground
Separate suitable supply         -> servo/ESC/load power
```

Do not assume the ESC’s BEC is suitable for the ECU or that two power sources can be paralleled. Verify pulse endpoints and direction with fuel disconnected.

### Analog pressure, flame, torque, flow, or voltage sensor

For a typical three-wire analog sensor:

```text
Sensor supply -> correct regulated sensor voltage
Sensor ground -> ECU sensor ground
Sensor output -> ADC1 pin through required divider/filter/protection
```

- Confirm whether the sensor output rises or falls with the measured value.
- Confirm its output range at the ECU pin, not only at the sensor connector.
- A 5 V-powered 0.5–4.5 V pressure transducer normally needs scaling before the ESP32.
- Capture zero with the real plumbing depressurized, then calibrate against a trusted gauge/reference.
- Battery/bus measurement always needs a divider and suitable over-voltage protection.

### Potentiometer throttle or idle input

```text
Potentiometer end 1 -> 3.3 V
Potentiometer end 2 -> GND
Potentiometer wiper -> configured ADC1 pin
```

A value around 10 kΩ is commonly practical, but verify noise and current for the actual installation. Calibrate both endpoints after wiring. For an RC/servo-PWM input, use the configured digital input type instead of wiring it as analog; ensure the pulse signal is 3.3 V compatible.

### N1/N2 RPM sensor

OpenTurbine expects a clean pulse signal and the correct pulses-per-revolution value.

```text
Hall/conditioner output -> configured RPM input
Sensor ground           -> ECU ground
Sensor supply           -> voltage required by sensor
```

- The GPIO must never receive more than 3.3 V.
- An open-collector/open-drain sensor needs a pull-up to 3.3 V.
- A magnetic pickup normally needs a dedicated zero-crossing/comparator conditioner; do not connect an unbounded pickup waveform directly.
- Set pulses per revolution from the actual target geometry and verify displayed RPM with an independent tachometer before enabling overspeed protection.

### TOT/EGT or TIT thermocouple

Thermocouples connect to a supported converter module, not directly to the ESP32. Hardware supports MAX6675, MAX31855, and MAX31856 configurations.

```text
Thermocouple -> converter module
Converter CLK/MISO/(MOSI) -> configured ESP32 SPI pins
Converter CS              -> its configured chip-select pin
Converter GND             -> ECU ground
Converter supply          -> module-compatible supply
```

- Several SPI temperature modules may share CLK, MISO, and MOSI, but each requires its own CS.
- MAX31856 requires MOSI because the ECU configures and verifies the chip.
- Match thermocouple type, polarity, connector metals, and extension wire.
- Mount the probe at the temperature station whose limit you are configuring.

### NTC or other analog temperature sensor

The built-in NTC datasheet mode assumes:

```text
3.3 V -> fixed pull-up resistor -> ADC pin -> NTC -> GND
```

Enter the real pull-up resistance, NTC R₀, and beta value. For another analog circuit or sensor curve, use four well-spaced known-temperature points on Calibration instead.

### DS18B20 temperature sensor

Connect VCC to the sensor’s supported supply, GND to ECU ground, and DATA to the configured pin. A typical three-wire installation uses about a 4.7 kΩ pull-up from DATA to 3.3 V. Avoid parasite-power wiring for a noisy engine installation.

### Current sensor

Route the measured conductor through or across the current sensor as its manufacturer specifies. Connect the sensor’s conditioned output to an ADC1 pin and keep it within 0–3.3 V. Calibrate using zero plus a trusted current measurement, or enter datasheet zero voltage and mV/A sensitivity measured at the ECU pin.

### Start, stop, arm, and other digital inputs

Wire switches according to the configured active level. A common active-low arrangement is:

```text
Configured input -> switch -> GND
```

with a pull-up keeping the released input high. For long/noisy wiring, use suitable filtering, shielding, transient protection, and a fail-safe circuit. The physical STOP must be tested independently and should remove fuel or actuator power even if the ESP32 or software is unavailable.

## First setup

The web interface is organized in the order a new installation should normally follow:

1. **Hardware** — enable only what is physically fitted, select signal types, and assign pins.
2. **Config → Setup** — enter verified engine limits, oil targets, and essential behavior.
3. **Calibration** — calibrate inputs and find minimum reliable pump outputs.
4. **Sequence** — review startup and shutdown blocks and their conditions.
5. **Tools** — test one output at a time with fuel and ignition made safe.
6. **Dashboard** — perform dry sequences before any fueled attempt.

Settings that cannot apply to the fitted hardware are hidden in Setup. Advanced view keeps deeper tuning available and ghosts temporarily inactive settings.

## Complete first-time procedure

This is the literal path from an unopened board to a dry-tested ECU.

1. **Install the firmware.** Download the official setup tool, connect the board with a data-capable USB cable, and choose **Clean install / reinstall**. This erases the selected board, so use it only for a blank board or an intentional fresh installation. The tool detects the ESP32 target; if several connected boards are listed, choose the intended board and confirm the detected target. Wait for both firmware and web files to finish.
2. **Power only the ECU.** Keep every actuator/load power supply disconnected. Leave fuel and ignition energy physically isolated.
3. **Join the ECU Wi-Fi.** A fresh default installation advertises `OpenTurbine` with no password. Join it and open `http://192.168.4.1`. If you later change the profile id, the Wi-Fi name changes with it.
4. **Acknowledge the safety notice and choose a theme.** Confirm that the Dashboard loads and remains connected.
5. **Set identity and Wi-Fi security.** In Hardware, set a recognizable profile id and an 8–63 character AP password if the ECU should not remain open. Save, let it reboot, then reconnect using the new network name/password.
6. **Configure one device at a time.** In Hardware, select the correct target, enable only fitted devices, choose electrical types, assign pins, and resolve every warning. Save and reboot.
7. **Verify the saved hardware.** Reopen Hardware after reboot and compare every pin/type with the wiring before applying actuator power.
8. **Check live inputs.** Power sensors only. Open Dashboard/Calibration and confirm plausible raw/live response. Do not enable a safety based on an uncalibrated sensor.
9. **Enter verified essential settings.** Use Config → Setup. Do not use example suggestions as authority.
10. **Calibrate inputs and pump minimums.** Follow the visible Calibration wizards with fuel/ignition made safe.
11. **Review sequences.** Confirm startup and shutdown order, timing, sensor gates, and abort behavior.
12. **Test outputs at logic level.** With load power still isolated, confirm output polarity using a meter or test indicator.
13. **Connect and test loads individually.** Use Tools in STANDBY. Begin with short duration and conservative proportional output. Keep fuel and ignition separated until each path is proven.
14. **Test every stop path.** Verify web STOP, physical STOP, loss-of-signal behavior, and fuel shutoff. Do not continue if any stop path is ambiguous.
15. **Run complete dry sequences.** Fuel disconnected, ignition disabled where appropriate, and turbine restrained. Confirm mode/block progress and output order.
16. **Back up the engine file.** Download it from Tools and store it securely.
17. **Perform the pre-start review below.** Only then plan a controlled fueled test.

### 1. Hardware

Open **Hardware** and describe the actual installation. Do not enable a sensor or actuator merely because it appears in the list.

- Use **Installed Channel Inventory** for channels that rules, sequences, and controller bindings need to reference by ID. The stable ID is the machine key; keep it short, unique, and unchanged after other features reference it. The display name is safe to edit.
- Inventory inputs can be digital, analog, pulse/frequency, or RC PWM. Digital switch roles can feed existing DI behaviors such as inhibit-start, E-stop, AB arm/fire, limp mode, sequence gate, and fault inputs. Registry-driven DI behavior currently uses the existing active-low/pull-up default unless a legacy DI channel supplies richer switch metadata.
- Inventory outputs can be relay, PWM, or servo/ESC. Relay outputs quantize at the driver boundary; PWM and servo outputs preserve the full 0-100% demand used by rules, sequences, tools, and controllers.
- Repeatable outputs with the same role are allowed. For example, `Oil Pump 1` can be the main bound pump while `Oil Pump 2` is controlled by rules, sequences, tools, or another oil loop.
- The standard `AB igniter` inventory output bridges to the existing Igniter 2 / afterburner ignition path when it uses the standard `ab_igniter` or `igniter2_main` ID.
- Confirm the correct ESP32 target.
- Assign each GPIO once; resolve every conflict reported by the page.
- Select the real electrical output type: relay, PWM, servo/ESC, or other offered mode.
- For output inventory channels, set boot-safe and fault-safe demand deliberately. Relay outputs still switch at the driver boundary, while PWM and servo/ESC outputs preserve proportional demand.
- Configure sensor chips and inputs exactly as wired.
- Enable safety functions only after their required sensors are fitted and calibrated.
- Save Hardware and allow the ECU to reboot.

After reboot, return to Hardware and verify that every saved device and pin is still correct.

### 2. Essential configuration

Open **Config → Setup**. Example suggestions are editable examples only; they are not safe values for your turbine.

Review at least:

- Maximum and minimum running shaft RPM
- Selected engine-temperature source and hard temperature limit
- Temperature warning/pullback margin
- Oil prime target, minimum pressure before ignition, running target, and low-oil shutdown threshold
- Throttle opening and closing rate
- Flameout source and delay
- Hot-start threshold
- Every fitted optional safety threshold

If using multiple oil systems, define each oil loop with its pressure input, pump output, target pressure, deadband, and min/max demand. The first enabled loop feeds the primary oil controller; additional enabled loops drive their own selected registry pump outputs before rules run.

`0` can mean disabled, automatic, or unlimited depending on the field. Read the description beside that specific control.

### 3. Calibration

Calibrate only while the engine is in STANDBY/FAULT and the installation is made safe.

- **Fuel pump minimum:** run the slow sweep, stop when the pump first turns, fine-adjust, and verify several reliable restarts before saving.
- **Oil pressure:** capture the correct ambient zero reference, then use physical gauge points, automatic fitting, an explicit straight/curved fit, or sensor sensitivity in mV/bar.
- **Oil pump minimum:** sweep upward slowly and stop when the pump or pressure response begins.
- **Throttle and idle inputs:** hold each endpoint during the one-second capture. The ECU adds a small endpoint margin.
- **Flame sensor:** capture the no-flame noise floor with the igniter operating. Keep the threshold close enough to detect weak light-off flame. The last-run average is reference information, not an automatic threshold.
- **Temperature:** use NTC datasheet values or four well-spaced known temperature points.
- **Current sensors:** use captured zero/reference current or datasheet zero voltage and mV/A sensitivity at the ECU ADC pin.

Never calibrate pressure or flow using an unverified web reading as its own reference.

### 4. Startup and shutdown sequence

Open **Sequence** and read every block in order. A block being available does not mean it belongs in your engine.

Confirm that:

- Oil flow/pressure is established before ignition when required.
- The starter or air source reaches the required ignition speed.
- Ignition is active before fuel is introduced.
- Fuel begins at a deliberately conservative value.
- Light-off is confirmed by a fitted, calibrated source or an explicitly accepted timed method.
- The engine reaches self-sustaining speed without losing lubrication.
- STOP closes fuel immediately.
- Shutdown keeps oil/scavenge/cooling outputs active for as long as the engine requires.

Use dry runs with fuel physically disconnected before a fueled test.

### 5. Bench-test outputs

Open **Tools** in STANDBY. The page shows only tests whose hardware is fitted. Use **Test settings** or the gear button on a test card to edit duration and proportional output.

Test outputs individually:

1. Fuel shutoff direction with no fuel pressure
2. Oil and scavenge pumps
3. Starter enable and starter direction
4. Igniter with fuel absent and vapors cleared
5. Throttle/fuel actuator direction and travel
6. Cooling fans, bleed valves, auxiliary pumps, and other accessories

The browser is not the emergency stop. Verify the independent physical stop before continuing.

## Pre-start review

Before every first fueled run or major configuration change:

- Back up the full engine file from Tools.
- Confirm Hardware has no pin or dependency errors.
- Confirm every safety-critical sensor is healthy and calibrated.
- Compare Config limits against authoritative engine and sensor information.
- Run the startup sequence dry.
- Confirm STOP cuts every fuel path from web, physical input, and any external controller.
- Confirm loss of browser/Wi-Fi cannot leave a manual test running indefinitely.
- Inspect fuel and oil plumbing for correct routing, restraint, leaks, and heat protection.
- Clear the exclusion zone and prepare fire suppression.

For a first light, use conservative fuel, short attempts, immediate abort criteria, and a second person where possible.

## Updating, backup, and recovery

### Back up

In **Tools**, download the full engine file before an update. It contains hardware, settings, sequences, calibration, and the ECU Wi-Fi AP password. Treat it as sensitive.

### Update

Use **OpenTurbine Setup Tool → Update and keep my setup** for normal Wi-Fi updates. It backs up the engine settings and does not perform a factory reset. Use **Clean install / reinstall** for a blank board, corrupted installation, intentional clean start, or partition-table change; that USB path erases everything on the selected board.

Do not interrupt power during firmware or web-asset installation.

### Recovery

- If the ECU Wi-Fi appears but pages fail, reinstall/update the web assets.
- If the board does not appear over USB, try a data cable, another USB port, the driver button offered by the setup tool, and the board’s BOOT/RESET procedure.
- If a configuration error puts the ECU in FAULT, the web interface remains available for repair.
- If a restored engine file is rejected, verify that it is a complete matching OpenTurbine `ecu_config.json`, not only one section.
- Factory reset erases hardware, settings, calibration, and logs. Back up first.

## Troubleshooting index

Use Ctrl+F for the symptom or keyword.

| Symptom | Checks |
|---|---|
| Download says Not Found | No GitHub release with the required asset exists yet. Use only the official Releases page. |
| Windows blocked installer / SmartScreen | Verify the official URL, then use Downloads → Keep or SmartScreen → More info → Run anyway. |
| No COM port / board not detected | Data cable, direct USB port, setup-tool-selected CP210x/WCH driver, reconnect only when prompted, close serial monitors. |
| Upload cannot connect / bootloader timeout | Hold BOOT, tap EN/RESET, start upload, release BOOT when connection begins. |
| OpenTurbine Wi-Fi missing | Confirm successful firmware boot and power; reinstall over USB; check serial output if developing. |
| Wi-Fi connects but page does not open | Browse directly to `http://192.168.4.1`; disable mobile-data/VPN captive routing temporarily; reinstall web assets if necessary. |
| Page reports disconnected | Stay on the ECU Wi-Fi, reload once, check ECU power/noise, and verify another client is not overwhelming the AP. |
| Hardware save rejected | Return to STANDBY/FAULT; resolve missing pins, conflicts, unsupported types, and dependent safety/controller settings. |
| Config field hidden | Use Setup only for applicable essentials; use Advanced for deeper applicable fields; confirm prerequisite hardware is fitted. |
| Calibration command rejected | ECU must be STANDBY/FAULT, required hardware must be fitted, and no other actuator test may be active. |
| Sensor reads zero/full-scale/fault | Verify supply, common ground, signal voltage, selected pin/type, ADC1 use, divider, polarity, and calibration. |
| RPM is wrong | Verify pulse conditioning, 3.3 V level, pulses per revolution, target geometry, and independent tachometer reading. |
| Temperature is wrong | Verify converter chip, thermocouple type/polarity/location, shared SPI wiring, unique CS, and cold-junction/module supply. |
| Pump/servo moves backward | Remove fuel/load, correct active level or servo/PWM endpoints/direction, then repeat Tools testing. |
| Engine enters FAULT at boot | Read the visible fault/config warning; Hardware, Config, Calibration, Tools, and restore remain available for repair. |
| Full engine restore rejected | Use the complete matching `ecu_config.json`; do not cross hardware/settings sections from different profiles. |
| Wi-Fi password forgotten | Recover over USB and restore/reset configuration. Factory reset removes all settings and calibration. |

## External display or custom controller

OpenTurbine provides the **OpenTurbine Cluster (OTC)** serial protocol for embedded displays and companion devices.

- [Start building a cluster or companion device](../examples/cluster/README.md)
- [Single-file Arduino/ESP32 client](../examples/OTCClusterClient.h)
- [Complete OTC wire protocol](OTC_CLUSTER_PROTOCOL.md)

OTC supports TX-only telemetry or optional two-way commands/subscriptions. Normal ECU safety and mode gates still apply to commands received over OTC.

## Support and reporting problems

Use the repository’s [Issues page](https://github.com/elia179/OpenTurbine/issues) for reproducible problems. Include:

- Firmware version and ESP32 target
- What hardware is actually fitted
- The exact operating mode and sequence block
- Expected and observed behavior
- Relevant Event Log or Session Data
- A sanitized engine file when safe to share—remove the Wi-Fi password first

Do not publish secrets, personal network information, or an unreviewed engine file.

## Developers and beta testers

Most users can stop reading here. Source layout, manual builds, release packaging, validation tools, internal plans, and beta reporting are indexed in [Developer and beta documentation](README.md).

Quick developer build:

```bash
git clone https://github.com/elia179/OpenTurbine.git
cd OpenTurbine
pio run -e esp32dev
pio run -e esp32s3dev
```

Web sources live in `data_src/`; deterministic compressed assets are generated into `data/` with `python tools/gzip_data.py`.

## License

OpenTurbine is released under the [MIT License](../LICENSE).
