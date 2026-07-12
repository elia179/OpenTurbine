---
layout: document
title: Troubleshooting
lede: Find the symptom first; do not treat a setup or wiring question as a software bug.
---

## Setup Tool is blocked or will not start

Verify the download is the official [v0.5.23 release](https://github.com/elia179/OpenTurbine/releases/tag/setup-tool-v0.5.23). Its current EXE is unsigned, so SmartScreen may warn. Do not disable Windows protection globally.

## Board is not detected or flashing fails

Try a known data-capable cable and a direct USB port. Allow the Setup Tool to offer the matching CP210x or WCH driver; it rescans automatically after installation. Close serial monitors. If upload cannot enter the bootloader, use the board’s documented BOOT/RESET sequence.

## Wi-Fi or dashboard is unavailable

Confirm the install completed, join the board Wi-Fi, and browse directly to `http://192.168.4.1`. If Wi-Fi is visible but pages fail, reinstall the web assets. Do not interrupt power during update.

## Collect support diagnostics

Driver diagnostics are written to `%LOCALAPPDATA%\OpenTurbine\SetupTool\logs`; Wi-Fi-update backups live in `Documents\OpenTurbine\Backups`. Remove secrets, especially Wi-Fi passwords, before sharing them through [Setup Help](https://github.com/elia179/OpenTurbine/issues/new?template=setup_help.yml).
