---
layout: document
title: Get started
lede: Install a new supported board from Windows without cloning the source code.
---

{% include safety-note.html %}

## Before you begin

You need a Windows computer, a data-capable USB cable, and either a Classic ESP32 with at least 4 MB flash or an ESP32-S3 DevKitC-1 N16R8 target. Keep fuel, ignition energy, starter power, and load power physically isolated during installation.

## Download and connect

1. [Download OpenTurbine Setup Tool for Windows]({{ site.data.project.windows_download_url }}).
2. Connect the board directly to the computer using a data-capable USB cable.
3. Open the Setup Tool. You should see a detected board or a prompt that identifies the missing USB driver.
4. For a new board select **Clean install / reinstall**. It erases the selected board.
5. For a working controller select **Update and keep my setup**. Back up the engine file first; this Wi-Fi path preserves setup intentionally.

If Windows warns about the unsigned v0.5.23 tool, confirm it came from the official OpenTurbine release before choosing Windows’ “More info” and “Run anyway” path. Do not disable Windows protection globally.

## Connect to the dashboard

After a successful install, join the board’s Wi-Fi network and open `http://192.168.4.1`. What you should see is the OpenTurbine dashboard. If it does not appear, use the [symptom-based troubleshooting guide]({{ '/troubleshooting/' | relative_url }}).

## First dry setup

Follow this safe order: Hardware → Config → Calibration → Sequence → Tools → Dashboard. Verify pins and input readings, review limits and sequences, then test outputs at logic level before connecting loads. Do not add fuel until every shutdown path, including the independent physical stop, has been tested.

Need help? Use the [Setup Help form](https://github.com/elia179/OpenTurbine/issues/new?template=setup_help.yml) and attach only sanitized diagnostics.
