# OpenTurbine Beta User Guide

This guide is for beta testers setting up one ECU from a fresh flash to a
controlled first test. It assumes you have already read the safety warning in
`README.md`.

## 1. Safety Baseline

- Use a physical STOP / fuel-cut switch. The web STOP button is supplementary.
- Keep fuel disabled for the first setup, calibration, and actuator tests.
- Keep the engine restrained and clear of people, loose parts, and flammable
  items.
- Use Bench Mode only for dry bench testing. Do not use Bench Mode with fuel.
- Back up `ecu_config.json` before live testing and after every known-good
  setup change.

## 2. Flash The ECU

Choose the correct PlatformIO environment:

| Board | Environment |
|---|---|
| Classic ESP32 dev board | `esp32dev` |
| ESP32-S3 / YD-ESP32-S3 / YD-ESP32-23 | `esp32s3dev` |

Flash firmware and web assets:

```bash
pio run -e esp32dev -t upload
pio run -e esp32dev -t uploadfs
```

For ESP32-S3:

```bash
pio run -e esp32s3dev -t upload
pio run -e esp32s3dev -t uploadfs
```

Notes:

- If upload does not connect, hold BOOT, tap EN/RESET, then release BOOT when
  upload starts.
- Firmware upload updates the ECU program.
- Filesystem upload updates the web pages.
- OTA firmware and web-asset updates are also available later from Tools.

## 3. First Connection

1. Power the ECU.
2. Connect to the ECU Wi-Fi AP. The SSID is the engine file
   `hardware.profile_id`; on a new file it comes from `OT_PROFILE_ID`.
3. Open `http://192.168.4.1`. `http://ot.local` may also work when mDNS is
   supported by your computer or phone.
4. If a captive portal opens, use it only as a convenience. Direct browser
   navigation to `192.168.4.1` is the reliable method.

## 4. Hardware Page First

Open Hardware before changing Config.

1. Set the engine profile ID and description.
2. Select the fitted sensors and actuators.
3. Assign GPIO pins.
4. Review warnings for duplicate, unsafe, or missing pins.
5. For ESP32-S3/YD boards, the onboard status LED is normally:
   - Status LED enabled
   - Type: `NeoPixel RGB data LED`
   - Pin: GPIO48
6. For a normal external LED, choose `Plain GPIO on/off` and select the GPIO.
7. Save Hardware and reboot when prompted.

After reboot, revisit Hardware and confirm:

- The expected sensors/actuators are still enabled.
- No required pin is missing.
- No profile/platform warning is shown.

## 5. Config Page

Set only the limits and features that match fitted hardware.

Minimum settings to review:

- selected EGT source and temperature limits
- oil pressure target and minimum pressure
- startup and shutdown timings
- throttle ramp rates and idle range
- safety enables
- flameout source
- telemetry rate

Flameout does not require a flame detector. In Auto mode it uses the best fitted
source in this order:

1. flame sensor
2. N1 RPM drop
3. selected EGT drop

If none of those are fitted, flameout monitoring is not usable.

Auto-relight requires N1 RPM feedback and an igniter.

## 6. Calibration

Calibrate every fitted input before using it for control or safety.

| Input | What to check |
|---|---|
| Throttle / idle input | Live raw value moves smoothly through full travel; endpoints captured correctly |
| Oil pressure | Raw value changes with known pressure; saved curve reads near zero at no pressure and near reference at applied pressure |
| P1 / P2 / fuel pressure | Two-point calibration matches the sensor datasheet or known references |
| Battery voltage | Displayed voltage matches a meter at the ECU input |
| Fuel flow | Pulse or analog scaling matches the physical sensor |
| Flame sensor | Noise floor captured with no flame; threshold is above noise and below confirmed flame reading |
| AB flame sensor | Same threshold logic as flame sensor, but only when a dedicated AB flame sensor is fitted |

If a calibration card is missing, go back to Hardware and confirm that sensor is
enabled and saved.

## 7. Dry Tests Before Fuel

Run these with fuel disabled or physically disconnected:

1. Confirm STOP input changes state correctly.
2. Confirm START input changes state correctly.
3. Use Tools to test each fitted actuator briefly.
4. Confirm every actuator moves the correct physical output.
5. Confirm active-high / active-low polarity is correct.
6. Confirm sensor readings are plausible and stable on Dashboard.
7. Review Sequence page for errors. Errors block START; warnings need review.
8. Run a dry START only when actuator motion is safe.
9. Confirm STOP immediately commands a safe shutdown path.

Do not continue if any actuator moves the wrong output, wrong direction, or does
not turn off.

## 8. First Fuel Test Checklist

Before fuel:

- `ecu_config.json` backed up from Tools.
- Hardware page has no missing required pins.
- Config page has correct limits and units.
- Calibration page shows all fitted inputs and plausible readings.
- Sequence page has no errors.
- STOP input and physical fuel cut are verified.
- Battery / supply voltage is stable under actuator load.
- Oil system has been primed and verified.
- Fire safety equipment is present.

During first light-off:

- Keep one operator on physical STOP.
- Watch selected EGT, oil pressure, RPM, and mode.
- Abort on unexpected actuator behavior, missing oil pressure, fast EGT rise,
  wrong sensor units, or unclear UI state.

After the run:

- Download logs.
- Back up `ecu_config.json` if the setup is still considered good.

## 9. Backup, Restore, And Factory Reset

Use Tools:

- Download full engine file: saves the complete `ecu_config.json`.
- Restore full engine file: restores hardware and settings together.
- Factory reset: restores shipped defaults and forces hardware review again.

Do not mix hardware from one engine file with settings from another. Profile ID
mismatch locks engine operations because mixed engine data is unsafe.

## 10. Firmware And Web UI Updates

There are two update paths:

- Firmware OTA: updates the ECU program.
- Web UI Assets Update: updates HTML/CSS/JS pages.

Both require STANDBY. Do not update while outputs are active.

After updating:

1. Reconnect to the ECU.
2. Hard-refresh the browser if the UI looks stale.
3. Check Dashboard version.
4. Check Hardware and Config for warnings.
5. Back up the engine file if everything is correct.

## 11. Troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| Page does not load | Wi-Fi not connected, browser cached old page, filesystem not flashed | Connect to ECU AP, open `192.168.4.1`, hard-refresh, flash/upload web assets |
| Page is unstyled or half-loaded | Missing or stale web assets | Run `uploadfs` or use Tools > Web UI Assets Update |
| Dashboard slow after connect | Browser/WebSocket reconnect or stale page cache | Hard-refresh once; verify web assets are current |
| `Profile ID Mismatch` | Hardware and settings sections came from different engine files | Restore one complete `ecu_config.json` or save Hardware to synchronize IDs |
| Config page says reconnecting | WebSocket disconnected | Refresh page; check ECU power and Wi-Fi signal |
| Calibration card missing | Sensor/input not enabled in Hardware | Enable sensor, save Hardware, reboot |
| P1/P2 not shown | Pressure sensors disabled or not saved | Enable P1/P2 in Hardware and save |
| Oil/flame calibration appears with no sensor | Hardware file is stale or sensor flag still enabled | Disable the sensor in Hardware, save, reboot |
| S3 status LED dark | Wrong LED type, wrong pin, RGB jumper open, or board variant differs | Select NeoPixel RGB on GPIO48; if still dark, test the board LED separately |
| START blocked | Sequence validation error, profile mismatch, not STANDBY, or active tool timer | Read Dashboard/Sequence error, return to STANDBY, fix Hardware/Config |
| Tool command ignored | Engine not in STANDBY or another tool/test active | Return to STANDBY and wait for active test to expire |
| OTA rejected | Not in STANDBY or controlled output active | Stop engine, wait for all outputs/tools off, retry |
| Logs missing | No completed run or flash cleanup removed old sessions | Run again and download soon after test |

## 12. What To Report During Beta

Report:

- board type and environment (`esp32dev` or `esp32s3dev`)
- firmware/UI version shown on Dashboard
- exact page and action
- screenshot of warning/error
- downloaded `ecu_config.json` if safe to share
- log/session file when the issue involves a run
- whether the issue is repeatable after refresh/reboot

