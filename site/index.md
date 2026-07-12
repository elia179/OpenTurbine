---
layout: landing
title: Open-source turbine ECU for ESP32
description: Guided Windows setup, configurable engine sequences, monitoring, logging, and a browser dashboard.
---

<section class="hero"><div class="shell hero-grid"><div>
<p class="eyebrow">Experimental turbine control</p>
<h1>Open-source turbine ECU software for ESP32</h1>
<p class="lede">Guided Windows setup, configurable engine sequences, monitoring, data logging, and a browser-based dashboard.</p>
{% include download-cta.html %}
<p>For experimental turbojets, APUs, generators, turboshafts, turboprops, and turbine test rigs.</p>
{% include safety-note.html %}
</div><div><img class="screenshot" src="{{ '/assets/images/hero-dashboard.png' | relative_url }}" alt="Current OpenTurbine browser dashboard showing engine temperature, RPM, oil, fuel, and actuator status"></div></div></section>

<section class="section alt"><div class="shell"><h2>Start with a supported board</h2><div class="steps"><div class="step"><h3>Connect</h3><p>Connect a supported ESP32 board with a USB data cable. The Setup Tool should detect a serial port.</p></div><div class="step"><h3>Install</h3><p>Run the OpenTurbine Setup Tool on Windows and choose the path that matches a new or existing controller.</p></div><div class="step"><h3>Configure</h3><p>Connect to the board’s Wi-Fi, open the browser dashboard, and complete dry setup before adding fuel.</p></div></div></div></section>

<section class="section"><div class="shell"><h2>What OpenTurbine does</h2><div class="card-grid three"><div class="card"><h3>Engine sequence control</h3><p>Configure startup and shutdown sequences, fuel and oil-pump control, starter and ignition control.</p></div><div class="card"><h3>Monitoring and protection</h3><p>Monitor RPM and temperature, define fault limits and shutdown actions, and review event and run logs.</p></div><div class="card"><h3>Configure without compiling</h3><p>Use the browser dashboard for hardware configuration, calibration, backup, and Wi-Fi updates that preserve setup.</p></div></div></div></section>

<section class="section alt"><div class="shell"><h2>Compatibility</h2>{% include compatibility-table.html %}</div></section>

<section class="section"><div class="shell"><h2>Choose your path</h2><div class="card-grid three"><a class="card" href="{{ '/get-started/' | relative_url }}"><h3>Try OpenTurbine on a board</h3><p>Install a supported board with the Windows Setup Tool.</p></a><a class="card" href="{{ '/hardware/' | relative_url }}"><h3>Build an ECU</h3><p>Plan drivers, sensors, wiring, power protection, and emergency shutdown.</p></a><a class="card" href="{{ '/troubleshooting/' | relative_url }}"><h3>Update or recover</h3><p>Back up an existing controller, update it, or diagnose installation problems.</p></a><a class="card" href="{{ '/developers/' | relative_url }}"><h3>Develop or integrate</h3><p>Build firmware, work with source, or integrate the serial protocol.</p></a></div></div></section>

<section class="section alt"><div class="shell"><h2>Your responsibilities remain physical</h2><p>OpenTurbine does not replace suitable pump, solenoid, or ignition drivers; correct sensors and conditioning; independent emergency shutdown; power protection and fusing; safe limits; restrained test equipment; or operating judgment.</p><img class="screenshot" src="{{ '/assets/images/system-overview.svg' | relative_url }}" alt="System overview showing ESP32 OpenTurbine between sensors, browser dashboard, driver electronics and loads, with an independent emergency stop path"></div></section>

<section class="section"><div class="shell"><h2>Get the right help</h2>{% include support-options.html %}</div></section>
