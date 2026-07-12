---
layout: document
title: Hardware guide
lede: Build a safe interface around the ESP32; a GPIO pin is a logic signal, never load power.
---

{% include safety-note.html %}

## Controller and power

Use only the supported targets listed on the [home page]({{ '/' | relative_url }}#compatibility). Supply the ESP32 from a clean regulated source within the board manufacturer’s limits. Fuse and protect every load supply, and keep starter, ignition, pump, and sensor wiring deliberately separated.

## Drivers, sensors, and grounding

Every pump, valve, starter, or ignition path needs suitable driver electronics between an ESP32 GPIO and the load. Confirm all GPIO signals remain 3.3 V compatible. Use appropriate signal conditioning for analog sensors and thermocouple converter modules for temperature probes. Verify every polarity and voltage with a meter before connecting it to the board.

## Emergency stop and dry wiring

Install a physical emergency fuel/power stop that removes fuel or load power independently of the browser, firmware, and Wi-Fi. Test it while fuel and ignition are isolated. Then use the Hardware page to select the target, enable only fitted hardware, assign each GPIO once, save, reboot, and verify the saved configuration.

For detailed electrical examples and pin rules, see the [hardware section of the repository guide](https://github.com/elia179/OpenTurbine#electrical-and-wiring-basics).
