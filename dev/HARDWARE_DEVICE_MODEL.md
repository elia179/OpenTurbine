# Hardware device model and UI plan

The Hardware page is device-first. A user chooses what a channel does, then
chooses how that device is electrically connected. Internal signal semantics
are derived from those choices and are not presented as the primary control.

## Card anatomy

Every installed card uses the same top-level order:

1. **Purpose** — the ECU function, such as N1 speed, throttle input, coolant
   temperature, oil pump, or generic output.
2. **Name** — editable dashboard/rule/sequencer label, initially set to a plain
   description of the purpose.
3. **Signal type** — only the electrical interfaces that make sense for the
   selected purpose.
4. **Pin or bus wiring** — GPIO for simple signals; explicit SPI/OneWire fields
   for devices that need them.
5. **Signal range and direction** — electrical endpoints and inversion. These
   are separate from operational calibration.
6. **Device options** — conversion factor, thermistor values, PWM timing,
   igniter dwell, wet-glow pilot fuel, or output current sensing.
7. **Boot safe state** — the output state written at boot before the sequencer,
   rules, or controller owns it. Fault shutdown always drives outputs off.
8. **Used by** — sequencer, rules, controller, safety, logging, and binding
   references.

Stable machine IDs remain hidden/read-only. Names are never used as stored
references.

## Input purposes and signal types

| Purpose | Supported signal types | Value exposed to firmware |
| --- | --- | --- |
| N1 speed | PCNT pulse, ADC transmitter | RPM |
| N2 speed | PCNT pulse, ADC transmitter | RPM |
| Additional shaft speed | pulse/frequency, ADC transmitter | RPM |
| TOT / EGT | MAX6675, MAX31855, MAX31856, analog transmitter | degrees C |
| TIT | MAX6675, MAX31855, MAX31856, analog transmitter | degrees C |
| Oil temperature | analog transmitter, NTC divider, DS18B20, optional thermocouple | degrees C |
| Coolant temperature | analog transmitter, NTC divider, DS18B20 | degrees C |
| Intake / ambient temperature | analog transmitter, NTC divider, DS18B20 | degrees C |
| Oil pressure | ADC transmitter | bar |
| Compressor inlet pressure (P1) | ADC transmitter | bar |
| Compressor discharge pressure (P2) | ADC transmitter | bar |
| Coolant pressure | ADC transmitter | bar |
| Fuel pressure | ADC transmitter | bar |
| Fuel flow | pulse/frequency, ADC transmitter | litres/min |
| Flame sensor | digital switch or ADC intensity | normalized 0..1 |
| Torque | ADC transmitter | Nm |
| Battery voltage | ADC through divider | volts |
| Throttle input | ADC, RC pulse width, frequency, PWM duty | normalized 0..1 |
| Idle input | digital, ADC, RC pulse width, frequency, PWM duty | normalized 0..1 |
| E-stop / inhibit / AB arm / AB fire / fault / sequence gate / limp mode | digital switch | inactive 0, active 1 |
| Generic input | digital, ADC, frequency, RC pulse width, PWM duty | normalized 0..1 |

Generic inputs have no engineering-unit calibration and cannot silently become
an engine sensor or operator control. They are available only to the sequencer,
rules engine, telemetry, and logging. Their electrical endpoints define the
normalization window. Input inversion is applied after normalization.

N1 and N2 pulse inputs use the hardware PCNT path and require pulses per
revolution. Their ADC alternative requires a zero voltage and mV-per-RPM
conversion factor. Other physical pulse devices use a purpose-specific factor,
such as pulses per litre for fuel flow.

Internal GPIO pull-up/down controls are shown only on digital or pulse inputs
where they can be useful. ADC inputs do not use internal pulls. An NTC card has
a separate, explicit divider-orientation selector because its calibrated
external resistor is not the same thing as an imprecise internal GPIO pull.

## Output purposes and signal types

| Purpose | Supported signal types | Device options |
| --- | --- | --- |
| Main fuel / throttle ESC | PWM, servo/ESC | electrical range, minimum reliable run calibration |
| Fuel pump | relay, PWM, servo/ESC | electrical range, minimum reliable run calibration, current sensor |
| Oil pump | relay, PWM, servo/ESC | electrical range, controller minimum, current sensor |
| Coolant pump | relay, PWM, servo/ESC | electrical range, minimum reliable run calibration, current sensor |
| Scavenge pump / cooling fan | relay, PWM, servo/ESC | electrical range, minimum reliable run calibration, current sensor |
| Starter | relay, PWM, servo/ESC | electrical range, current sensor |
| Air starter | relay | polarity, current sensor when useful |
| Pilot gas / start-fuel solenoid | relay | polarity, current sensor when useful |
| Air or fuel purge valve | relay | polarity, current sensor when useful |
| Fuel shutoff / starter enable / valves | relay | polarity, current sensor when useful |
| Igniter / AB igniter | relay or PWM | simple on/off, dwell/rest igniter, current-limited coil, current sensor |
| Glow plug | relay or PWM | dry glow or wet glow with pilot-fuel subcard, current sensor |
| Prop pitch | PWM or servo/ESC | electrical range, inversion, current sensor |
| Variable nozzle actuator | PWM or servo/ESC | electrical range, inversion, current sensor |
| Generic output | relay, PWM, servo/ESC | normalized command, electrical range, current sensor |

Generic outputs accept only a normalized 0..1 command from the sequencer or
rules engine. PWM and servo drivers map that command onto the selected physical
range; relay outputs quantize it to off/on.

Coolant-pump, pilot-fuel, purge-valve, and variable-nozzle cards deliberately
remain sequencer/rules outputs until a dedicated controller is justified. The
air-starter role uses the existing airstarter sequence blocks and test command.
This keeps the hardware inventory complete without adding unrelated tuning
controls to a basic installation.

## Dashboard presentation

Physically equivalent primary sensors use equivalent live cards. TOT and TIT
both show the current value, peak, limit gauge, warning state, and trend; N1
and N2 use the same speed-card layout and trend. The EGT rise rate is shown on
the temperature selected as the engine's active EGT source so a TOT-derived
rate is never presented as a TIT measurement, or vice versa.

Fuel/throttle and oil-pump command cards both show their current normalized
demand, mapped electrical output, gauge, and trend. Trend samples are cached in
the browser for page navigation and expire after 15 minutes. They are display
history only and are not written to ECU configuration or flash.

## Range and calibration rules

Electrical ranges stay on the hardware card:

- ADC: raw 0..4095 hardware window.
- RC input: pulse-width endpoints, normally 1000..2000 us.
- PWM-duty input: duty endpoints, normally 0..100%.
- Frequency input: Hz endpoints, or a physical conversion factor for a
  purpose such as RPM or flow.
- PWM output: physical duty range, normally 0..100%.
- Servo output: physical pulse-width range, normally 1000..2000 us.

Operational calibration is a separate layer. A pump's minimum reliable running
command never rewrites its PWM or servo endpoints. The card displays the
derived physical point, for example `15% command = 1150 us` for a 1000..2000 us
servo. For auxiliary pumps and fans, a nonzero command below this floor is
raised to the floor while an explicit zero remains off. The existing main-fuel
minimum-spin calibration retains its safety deadband: a below-minimum command
becomes off rather than being silently raised. Changing an endpoint marks the
operational calibration stale.

## Delivery plan

### Phase 1 — channel foundation and understandable cards

- Persist an explicit purpose independently of the internal semantic family.
- Add coolant-temperature and coolant-pump purposes.
- Add PWM-duty input sampling and input inversion.
- Keep generic I/O strictly normalized and automation-only.
- Drive signal-type choices, ranges, pull controls, and conversion fields from
  the selected purpose.
- Preserve and surface current-sensor, igniter-dwell, coil-dwell, and wet-glow
  subcards.
- Migrate existing registry cards by stable ID and current role without
  breaking stored rule/sequencer references.

### Phase 2 — purpose bindings and operational calibration

- Make core bindings an automatic consequence of selecting N1, N2, TOT,
  throttle, main fuel, and other singleton purposes; reject duplicate singleton
  ownership with a clear card-level error.
- Put pump minimum-reliable-run calibration on the device card, retain the
  electrical range separately, and display the calculated signal.
- Mark pump calibration stale when the signal type or endpoints change.
- Generalize the oil-controller minimum wording so it remains distinct from a
  motor's reliable-running threshold.

### Phase 3 — coolant behavior and verification

- Keep coolant temperature and coolant pump available to rules/sequences by
  default; add a simple temperature controller only if a real installation
  needs it, rather than making the basic Hardware page more complex.
- Add configuration round-trip, driver validation, normalized-I/O, PWM-duty,
  NTC orientation, current-sensor, and migration tests.
- Run both ESP32 and ESP32-S3 firmware builds plus browser UI audits and a
  rendered Hardware-page inspection.
