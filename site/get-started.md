---
layout: document
title: Install OpenTurbine on ESP32 from Windows
description: Install the OpenTurbine ESP32 turbine ECU on a supported board from Windows with the guided Setup Tool, USB driver help, flashing, and Wi-Fi connection steps.
lede: Install a new supported board from Windows without cloning the source code.
---

{% include safety-note.html %}

## Before you begin

You need a Windows computer, a data-capable USB cable, and either a Classic ESP32 with at least 4 MB flash or an ESP32-S3 DevKitC-1 N16R8 target. ESP32-C3 and other unlisted families are not supported by the normal setup path. Keep fuel, ignition energy, starter power, and load power physically isolated during installation.

## Download and connect

1. [Download OpenTurbine Setup Tool for Windows]({{ site.data.project.windows_download_url }}).
2. Connect the board directly to the computer using a data-capable USB cable.
3. Open the Setup Tool. You should see a detected board or a prompt that identifies the missing USB driver.
4. For a new board select **Clean install / reinstall**. It erases the selected board.
5. For a working controller select **Update and keep my setup**. Back up the engine file first; this Wi-Fi path preserves setup intentionally.

For normal Windows setup, download `OpenTurbineSetupTool.exe`. Do not use GitHub’s **Download ZIP** source-code button. If Windows warns about the tool, confirm it came from the official [OpenTurbine Releases](https://github.com/elia179/OpenTurbine/releases) page before choosing Windows’ “More info” and “Run anyway” path. Do not disable Windows protection globally.

## USB driver help

If Windows sees a CP210x or WCH bridge but has not created a COM port, the Setup Tool offers the matching driver. If the detected bridge already owns a COM port but the ESP32 does not answer, hold BOOT, tap EN/RESET, close other serial programs, and try again.

## Connect to the dashboard

After a successful install, join the board’s Wi-Fi network and open `http://192.168.4.1`. What you should see is the OpenTurbine dashboard. If it does not appear, use the [symptom-based troubleshooting guide]({{ '/troubleshooting/' | relative_url }}).

## First dry setup

Follow this safe order: Hardware → Config → Calibration → Sequence → Tools → Dashboard. Verify pins and input readings, review limits and sequences, then test outputs at logic level before connecting loads. Do not add fuel until every shutdown path, including the independent physical stop, has been tested.

Need help? Use the [Setup Help form](https://github.com/elia179/OpenTurbine/issues/new?template=setup_help.yml) and attach only sanitized diagnostics.
