---
layout: document
title: FAQ
lede: Quick answers for the normal Windows installation path.
---

## Do I need programming experience or source code?

No. The normal Windows setup uses the Setup Tool. Manual source builds are an advanced/developer path.

## Which board should I use?

A Classic ESP32 with at least 4 MB flash, or the ESP32-S3 DevKitC-1 N16R8 target.

## Does clean install erase settings? Can I update without erasing them?

Clean install/reinstall erases the selected board. Update and keep my setup is the normal Wi-Fi update path; back up first.

## Why does Windows warn about the Setup Tool?

The current Setup Tool release is unsigned. Download only from the official release and verify its checksum when needed.

## Is OpenTurbine certified or inherently safe?

No. It is experimental and requires independently verified limits, drivers, and physical shutdown protection.

## Does it work on macOS or Linux?

There is no graphical installer for macOS/Linux currently. Manual PlatformIO builds are documented for advanced users.
