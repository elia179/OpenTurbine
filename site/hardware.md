---
layout: document
title: Hardware and wiring guide
lede: Build a safe interface around the ESP32; a GPIO pin is a logic signal, never load power.
---

{% include safety-note.html %}

## Supported controller targets

Use a Classic ESP32 with at least 4 MB flash, or the supported ESP32-S3 DevKitC-1 N16R8 target. ESP32-C3 and other unlisted families are not supported by the normal Windows installer. Select the exact target in Hardware before assigning pins.

## Power, logic, and grounding

ESP32 GPIO is 3.3 V logic and is not 5 V tolerant. Use a clean regulated ECU supply, fuse load supplies, keep high-current wiring away from sensors, and use a deliberate common-ground or isolation plan. On classic ESP32, use ADC1 GPIO 32–39 while Wi-Fi is active; GPIO 34–39 are input-only and ADC2 is unreliable with Wi-Fi.

## Drivers and signal conditioning

An ESP32 pin does not power a pump, valve, starter, relay coil, glow plug, or igniter. Put suitable driver electronics, a protected MOSFET/relay/ESC input, and required flyback suppression between the GPIO and every load. Thermocouples require supported converter modules. RPM sensors require clean, conditioned 3.3 V-compatible pulses; do not connect an unbounded magnetic pickup directly to a GPIO.

## Emergency stop and dry wiring

Install a physical fuel/power stop that removes fuel or load power independently of the ESP32, browser, and Wi-Fi. With fuel and ignition isolated, select only fitted hardware, assign each GPIO once, save and reboot, verify inputs, then test output polarity at logic level before powering loads.

<p><a class="button" href="https://github.com/elia179/OpenTurbine/blob/main/docs/USER_GUIDE.md#electrical-and-wiring-basics">Open the complete electrical and wiring reference</a></p>
