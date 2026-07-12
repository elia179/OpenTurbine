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
</div><div><img class="screenshot" src="{{ '/assets/images/hero-dashboard.png' | relative_url }}" alt="OpenTurbine browser dashboard in a deterministic demo state, showing temperature, RPM, oil, fuel, and sensor cards"></div></div></section>

<section class="section alt"><div class="shell"><h2>Start with a supported board</h2><div class="steps"><div class="step"><h3>Connect</h3><p>Connect a supported ESP32 board with a USB data cable. Windows should detect the connected USB bridge or the Setup Tool will offer the matching driver.</p></div><div class="step"><h3>Install</h3><p>Run the Windows Setup Tool for a clean install, or use its update path to preserve a working controller setup.</p></div><div class="step"><h3>Configure</h3><p>Connect to the board Wi-Fi, open the dashboard in a browser, and complete dry setup before adding fuel.</p></div></div><p class="source-note"><strong>For normal Windows setup, download OpenTurbineSetupTool.exe.</strong> Do not use GitHub’s <em>Download ZIP</em> source-code button.</p></div></section>

<section class="section"><div class="shell"><h2>What OpenTurbine controls</h2><div class="card-grid three"><div class="card"><h3>Engine sequences</h3><p>Configure startup and shutdown sequences, fuel and oil-pump control, starter and ignition control.</p></div><div class="card"><h3>Monitoring and protection</h3><p>Monitor RPM and temperature, define fault limits and shutdown actions, and review event and run logs.</p></div><div class="card"><h3>Configuration without compiling</h3><p>Use the browser dashboard for hardware configuration, calibration, backup, and Wi-Fi updates that preserve setup.</p></div></div></div></section>

<section class="section alt"><div class="shell split"><div><h2>What the Setup Tool does</h2><p>The Windows Setup Tool detects a supported board, offers the matching CP210x or WCH driver when the connected bridge has no COM port, and can flash a new board or update an existing setup.</p><p>Driver and boot-mode advice is based on the connected device: a bridge already assigned a COM port receives BOOT/RESET guidance instead of a redundant driver prompt.</p><a class="button secondary" href="{{ '/get-started/' | relative_url }}">Read Windows setup steps</a></div><div class="card"><h3>Before you click</h3><p>Clean install/reinstall erases the selected board. Update and keep my setup is the normal path for a working ECU; back up first.</p><p>Windows may warn when a release is new or unsigned. Verify the official release and its checksum.</p></div></div></section>

<section class="section"><div class="shell"><h2>Configuration screens</h2><div class="image-grid"><figure><img class="screenshot" src="{{ '/assets/images/hardware-page.png' | relative_url }}" alt="OpenTurbine Hardware page in a deterministic simulator"><figcaption>Describe fitted hardware and assign pins.</figcaption></figure><figure><img class="screenshot" src="{{ '/assets/images/calibration-page.png' | relative_url }}" alt="OpenTurbine Calibration page in a deterministic simulator"><figcaption>Calibrate inputs and outputs with fuel and ignition made safe.</figcaption></figure><figure><img class="screenshot" src="{{ '/assets/images/sequence-page.png' | relative_url }}" alt="OpenTurbine Sequence editor in a deterministic simulator"><figcaption>Review startup and shutdown sequence blocks.</figcaption></figure></div></div></section>

<section class="section alt"><div class="shell"><h2>Compatibility</h2>{% include compatibility-table.html %}<p class="quiet">ESP32-C3 and other unlisted ESP32 families are not supported by the current normal setup path.</p></div></section>

<section class="section"><div class="shell"><h2>Choose your path</h2><div class="card-grid four"><a class="card" href="{{ '/get-started/' | relative_url }}"><h3>Try on a board</h3><p>Install a supported board with the Windows Setup Tool.</p></a><a class="card" href="{{ '/hardware/' | relative_url }}"><h3>Build an ECU</h3><p>Plan drivers, sensors, wiring, power protection, and emergency shutdown.</p></a><a class="card" href="{{ '/troubleshooting/' | relative_url }}"><h3>Update or recover</h3><p>Back up an existing controller, update it, or diagnose installation problems.</p></a><a class="card" href="{{ '/developers/' | relative_url }}"><h3>Develop or integrate</h3><p>Build firmware, work with source, or integrate the serial protocol.</p></a></div></div></section>

<section class="section alt"><div class="shell"><h2>Your responsibilities remain physical</h2><p>OpenTurbine does not replace suitable pump, solenoid, or ignition drivers; correct sensors and conditioning; independent emergency shutdown; power protection and fusing; safe limits; restrained test equipment; or operating judgment.</p><img class="system-diagram" src="{{ '/assets/images/system-overview.svg' | relative_url }}" alt="System overview showing sensors and controls to OpenTurbine, the browser dashboard, driver electronics and loads, plus an independent physical emergency stop"></div></section>

<section class="section"><div class="shell"><h2>Get the right help</h2>{% include support-options.html %}</div></section>
