---
layout: document
title: User guide
lede: Configure and dry-test an OpenTurbine installation in a deliberate order.
---

{% include safety-note.html %}

## Contents

1. [Install and connect](#install-and-connect)
2. [Select hardware](#select-hardware)
3. [Limits and calibration](#limits-and-calibration)
4. [Sequences and dry tests](#sequences-and-dry-tests)
5. [Backup, update, and logs](#backup-update-and-logs)

## Install and connect

Use the [Get Started guide]({{ '/get-started/' | relative_url }}) to install a supported board. Keep fuel, ignition, starter power, and load power isolated. Join the board Wi-Fi and open `http://192.168.4.1`.

## Select hardware

In Hardware, choose the board target, enable only devices actually fitted, assign every GPIO once, save, reboot, and confirm the saved configuration. Never connect an unknown-voltage signal to a GPIO.

## Limits and calibration

Enter limits from authoritative engine and sensor information. Calibrate fitted inputs and pump minimums with fuel and ignition made safe. A suggested value is not an approved limit for a particular turbine.

## Sequences and dry tests

Read every startup and shutdown block. Test one output at a time, verify the independent physical stop, and run complete dry sequences before planning a fueled attempt.

## Backup, update, and logs

Download the full engine file before significant changes. Use **Update and keep my setup** for a working controller, and use Clean install/reinstall only when erasing the selected board is intended. Keep backups private because they can contain Wi-Fi credentials.

<p><a class="button" href="https://github.com/elia179/OpenTurbine/blob/main/docs/USER_GUIDE.md">Open the complete user guide</a></p>

<p class="document-nav"><a href="{{ '/get-started/' | relative_url }}">← Get Started</a><a href="{{ '/hardware/' | relative_url }}">Hardware →</a><a href="{{ '/troubleshooting/' | relative_url }}">Troubleshooting →</a></p>
