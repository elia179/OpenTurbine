---
layout: document
title: User guide
lede: A safe working order for configuring and testing an OpenTurbine installation.
---

{% include safety-note.html %}

## Normal workflow

1. Install and connect to the dashboard.
2. Select hardware and verify every configured pin.
3. Enter verified engine limits; examples are not authoritative limits.
4. Calibrate fitted sensors and pump minimums with fuel/ignition made safe.
5. Review each startup and shutdown block.
6. Test outputs safely in Tools, one at a time.
7. Run dry sequences, verify every stop path, then prepare a controlled fueled test.
8. Back up the full engine file before significant changes or updates.

The full operating and calibration reference remains available in the [repository guide](https://github.com/elia179/OpenTurbine#first-setup). Keep backups private because they can contain the board Wi-Fi password.
