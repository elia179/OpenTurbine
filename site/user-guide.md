---
layout: document
title: "OpenTurbine user guide: setup, calibration and operation"
description: The complete OpenTurbine user guide covers installation, dashboard connection, hardware setup, turbine ECU calibration, sequences, dry testing, updates, logs, and recovery.
lede: Configure and dry-test an OpenTurbine installation in a deliberate order.
---

{% include safety-note.html %}

This guide is for the normal supported-board path: a Classic ESP32 with at least 4 MB flash or the supported ESP32-S3 DevKitC-1 N16R8 target. It is an experimental turbine ECU. Keep fuel, ignition, starter power, and other load power isolated while configuring the controller.

## Contents

1. [Installation](#installation)
2. [Connecting to the dashboard](#connecting-to-the-dashboard)
3. [Hardware configuration](#hardware-configuration)
4. [Basic settings](#basic-settings)
5. [Calibration](#calibration)
6. [Startup sequence](#startup-sequence)
7. [Shutdown sequence](#shutdown-sequence)
8. [Control rules](#control-rules)
9. [Dry testing](#dry-testing)
10. [Pre-fuel checks](#pre-fuel-checks)
11. [Operation](#operation)
12. [Backups](#backups)
13. [Updates](#updates)
14. [Logs](#logs)
15. [Recovery](#recovery)
16. [Advanced features](#advanced-features)

## Installation

Install OpenTurbine from Windows with the [guided Setup Tool]({{ '/get-started/' | relative_url }}). Connect only one intended board through a data-capable USB cable. Choose **Clean install / reinstall** for a blank board or an intentional fresh start; it erases the selected board. For a working controller, back up first and use **Update and keep my setup**.

The tool identifies the connected USB bridge. If that bridge has no COM port, it can offer the matching CP210x or WCH driver. If it already owns a COM port but the board does not answer, close serial-monitor programs and follow the board's BOOT/RESET guidance instead of reinstalling an unrelated driver.

## Connecting to the dashboard

After a successful installation, join the board Wi-Fi network and open `http://192.168.4.1`. A fresh dashboard starts with a safety acknowledgement and a first-run setup path. If the network is present but the page does not open, stay connected to the board Wi-Fi, browse directly to that address, and see [Troubleshooting]({{ '/troubleshooting/' | relative_url }}).

## Hardware configuration

Open **Hardware** before entering engine settings. Select the exact controller target, add only devices that are physically fitted, choose each electrical type, and assign each GPIO once. The Installed Channel Inventory gives each input and output a stable ID that sequences, control rules, controllers, and diagnostics can reference. Resolve every requirement, invalid-channel, and pin-conflict message before saving. Save and allow the controller to reboot, then reopen Hardware and compare the saved configuration with the wiring.

GPIO is 3.3 V logic, not a power output. Use appropriate signal conditioning and driver electronics for pumps, valves, starters, igniters, relays, and sensors. The [hardware and wiring guide]({{ '/hardware/' | relative_url }}) explains power, ADC, grounding, driver, and emergency-stop requirements.

## Basic settings

In **Config**, enter limits only from authoritative engine, sensor, and actuator information. Use the search box and the **Essentials**, **All settings**, **Changed**, and **Unavailable** filters to find and review settings. Review the selected temperature source, RPM limits, oil targets, throttle behavior, flameout behavior, and each fitted optional safety. A visible example or suggested value is not an approved setting for a particular engine.

Settings that require unavailable hardware are ghosted or hidden. The Unavailable filter explains why a setting cannot be used; developer controls expose deeper applicable options but do not make an unfitted sensor or driver safe to use. Changed fields and their cards turn yellow until the changes are saved or discarded.

## Calibration

Calibrate only while the ECU is in STANDBY or FAULT and every hazardous energy source is made safe. Confirm raw sensor direction and plausible live readings before enabling a related safety function.

- Calibrate pressure, flow, voltage, and current sensors against a trusted physical reference.
- Capture throttle and idle endpoints with the real input wiring and the correct signal type selected.
- Find pump minimums slowly with fuel and ignition isolated; repeat the result before saving.
- Verify RPM with an independent tachometer and use the actual pulses-per-revolution value.
- Check thermocouple type, polarity, converter selection, and probe location before trusting temperature limits.

## Startup sequence

Open **Sequence** and read every startup block in order. Confirm that lubrication, starter or air source, ignition, fuel introduction, light-off confirmation, and self-sustaining transition match the actual engine. A sequence block being available in the editor does not mean it is suitable for every turbine.

Use conservative verified limits and explicit abort behavior. Test block order without fuel before any fueled attempt.

## Shutdown sequence

Confirm that STOP removes fuel immediately and that required oil, scavenge, cooling, or purge actions remain active only as long as the engine needs. Verify the web STOP, an independent physical fuel/power stop, and expected loss-of-signal behavior separately. The browser, Wi-Fi, and firmware are not substitutes for an independent stop circuit.

## Control rules

The **Sequence → Control Rules** tab is for small, predictable automations outside the main startup and shutdown block order. Each rule owns one output and can either switch it at a threshold with hysteresis or map a fitted input linearly to a variable PWM/servo output.

- Choose **All states**, or limit the rule to Starting, Running, and/or Shutdown.
- When the rule leaves its selected states, or its input becomes unavailable, the output returns to the configured off value.
- Hysteresis prevents rapid on/off switching near a threshold. For an “above 100 °C” rule with 5 °C hysteresis, the output turns on above 100 °C and remains on until the input falls to 95 °C.
- Mapping clamps below the input minimum and above the input maximum. Use it for bench work such as testing a variable output from a potentiometer before assigning operational values.
- Keep one enabled rule per output. Use the final hardware safe state for fault behavior; rules do not override hardware fault safety.

Review the plain-language summary, active states, off value, and units before saving. Rule changes are stored with the complete engine configuration and require the same review discipline as sequence changes.

## Dry testing

With fuel disconnected and ignition energy disabled where appropriate, test one output at a time in **Tools**. Only fitted testable hardware is shown. Use **Test settings** to review each command duration and proportional output before running it. Start with logic-level or meter checks, then connect loads individually. Verify output polarity, timing, sensor response, sequence progress, and all stop paths. Run a complete dry sequence before considering a fueled test.

## Pre-fuel checks

Before introducing fuel, confirm all of the following:

- Hardware has no unresolved pin, dependency, or wiring error.
- Safety-critical sensors are fitted, plausible, and calibrated.
- Limits match authoritative engine and sensor information.
- Fuel and oil plumbing is restrained, routed safely, and checked for leaks.
- Fuses, driver electronics, grounding, and emergency shutdown are tested.
- The exclusion zone, fire suppression, hearing/eye protection, and a second person where appropriate are ready.

## Operation

Operate only on a restrained test setup and observe the dashboard, independent instruments, and the engine itself. Stop for unexpected temperature, RPM, oil, vibration, fuel, wiring, or actuator behavior. Do not rely on a network connection or a browser tab to remain available during a fault.

## Backups

Download the complete engine file from **Tools** before significant configuration changes and before updates. It contains hardware settings, calibration, sequences, control rules, and Wi-Fi credentials; event and session logs are separate downloads. Store the engine file securely and remove credentials before sharing diagnostics.

## Updates

Use **Update and keep my setup** for the normal working-controller update path. Use Clean install/reinstall only when a fresh board, recovery, or intentional erasure is required. Do not interrupt power while firmware or web assets are being installed. Read the [official release notes](https://github.com/elia179/OpenTurbine/releases) for current release-specific information.

## Logs

Use the Event Log and session data to investigate a repeatable problem. Record the board target, fitted hardware, mode, sequence block, expected behavior, and observed behavior. Setup Tool diagnostics are stored under `%LOCALAPPDATA%\OpenTurbine\SetupTool\logs`; sanitize logs and engine files before sending them through [Setup Help](https://github.com/elia179/OpenTurbine/issues/new?template=setup_help.yml).

## Recovery

If a board is not detected, first try a known data cable, a direct USB port, and the driver or BOOT/RESET guidance offered for the connected device. If the Wi-Fi is visible but pages fail, reinstall the web assets. If a restore is rejected, use a complete matching engine file rather than mixing configuration sections. See [symptom-based troubleshooting]({{ '/troubleshooting/' | relative_url }}) for detailed checks.

## Advanced features

Advanced configurations can use optional N2 feedback, multiple oil loops, flame sensing, relight, afterburner hardware, registry-backed channels, control rules, session logging, and the OpenTurbine Cluster protocol. Fit and validate the underlying hardware first; the user interface intentionally hides or locks options whose prerequisites are absent. Developers and integrators should use the [developer documentation]({{ '/developers/' | relative_url }}) and the source protocol references.

<p class="document-nav"><a href="{{ '/get-started/' | relative_url }}">Get Started</a><a href="{{ '/hardware/' | relative_url }}">Hardware</a><a href="{{ '/troubleshooting/' | relative_url }}">Troubleshooting</a></p>
