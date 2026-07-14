# Channel registry

Configuration version 4 introduces the bounded `channel_registry` section.
It contains separate input and output inventories, each keyed by a stable,
machine-safe ID. Labels are presentation-only and must never be persisted as
references. IDs are bounded to 19 visible characters plus the null terminator.
Each card also persists a user-selectable `purpose`, independently of the
internal driver-family `role`. The current 16-input, 16-output and 8-binding
registry targets the ESP32-S3 build; the classic ESP32 remains a compatibility
build rather than the registry sizing constraint. The classic target exposes
8 inputs, 8 outputs, and 4 bindings and allocates its live registry from the
heap to preserve linker DRAM; the ESP32-S3 exposes the full 16/16/8 inventory.

The hardware section also accepts up to two fixed-capacity `oil_loops`
definitions. Each loop persists a stable loop ID plus `pressure_input` and
`pump_output` channel IDs. Loading resolves those IDs to compact registry
indexes; the first enabled loop feeds the existing single oil-pressure
controller compatibility path, including min/max demand and deadband. Additional
enabled loops are validated for unique pump ownership, but independent runtime
multi-pump closed-loop control is a bounded runtime bridge: the first enabled
loop feeds the existing controller path, and additional enabled loops drive
their own non-core registry pump outputs before rules run. Secondary loops read
the selected registry pressure input value as normalized `0.0..1.0` and scale
it across the oil-loop target range of `0..20 bar`; a later schema can add
per-channel engineering-unit calibration without changing stable IDs. If a
secondary loop loses its pressure input, it immediately drives the pump to the
configured oil failsafe demand; the primary compatibility loop keeps the
existing delayed failsafe behavior.

Output demand is normalized to `0.0..1.0`. Relay drivers quantize at `0.5` at
the physical boundary; PWM and servo drivers map it across their configured
electrical endpoints. Every output has an explicit boot-safe demand. Fault
shutdown is deliberately non-configurable and drives every output fully off.
Proportional pumps may also define `min_run_demand`; it clamps a nonzero
operational demand before electrical mapping but leaves zero fully off. It does
not rewrite the PWM-duty or servo-pulse endpoints.

The parser rejects duplicate or illegal IDs, direction/driver mismatches,
invalid demand ranges, conflicting input/output pins, overflowing capacities,
bindings to nonexistent channels, and wrong-direction standard bindings such
as an output bound to `primary_n1`.

Digital, analog, pulse/frequency, RC-PWM, and PWM-duty registry inputs are
sampled by bounded runtime dispatch. Digital inputs publish `0.0` or `1.0`;
analog and pulse inputs map raw ADC counts or frequency through the channel
`min`/`max` range; RC-PWM inputs map pulse width through microsecond endpoints;
and PWM-duty maps the measured high/period ratio. Optional inversion happens
after normalization. Generic inputs remain normalized automation sources and
cannot satisfy engine-controller bindings.

Purpose-specific drivers may expose engineering values. N1, N2, and auxiliary
shaft inputs support hardware pulse counting or an analog RPM transmitter. The
analog path stores its zero point and millivolts-per-RPM factor. Temperature
purposes support their relevant transmitter, thermocouple, NTC-divider, or
DS18B20 interfaces; coolant temperature intentionally excludes exhaust
thermocouple chips. Intake/ambient temperature uses the same bounded
low-temperature interfaces. P1, P2, oil, fuel, and coolant pressure purposes
use analog transmitter calibration. NTC divider orientation is explicit and
separate from GPIO pull-up/down configuration.

Registry digital inputs may also use the existing switch behavior purposes
`fault`, `estop`, `inhibit_start`, `sequence_gate`, `ab_arm`, `ab_fire`, or
`limp_mode`. At load, those channels are bridged into the fixed four-slot DI
runtime adapter when a slot is available, using the channel name for UI/telemetry
labels. Pull-up, pull-down, active polarity, and debounce are persisted on the
channel. Legacy `di_channels` remain readable and are migrated into
`digital_switch`/behavior-role registry inputs when capacity permits.

Generic registry outputs are initialized to boot-safe demand, driven through
the same normalized relay/PWM/servo path as specialized outputs, and driven to
fault-safe demand during `allOff()` / fault shutdown. Core singleton ownership
is determined by standard core IDs, selected purpose, or explicit core binding.
The first card for a controller-owned purpose is driven by that controller;
additional repeatable pumps or fans remain registry outputs for rules and
sequences. Generic output purposes never become controller-owned implicitly.
Air starter is an explicit turbine actuator purpose. Pilot/start-fuel,
purge-valve, and variable-nozzle purposes are also explicit, but intentionally
remain automation outputs unless a future controller is added.

Control rules, sequence side actions, and custom sequence blocks now serialize
stable `source` / `target` IDs and resolve them once at load to compact runtime
handles. Legacy numeric fields remain readable for old files. The Sequence page
shows registry channels in the control-rule pickers and preserves missing
`source` / `target` IDs as disabled, labelled options so they are visible instead
of silently retargeted. Backend validation rejects explicit rule, side-action,
and custom-block IDs that do not resolve to an installed readable input or
writable output in the staged hardware/settings document.

Legacy files without a registry retain their existing singleton hardware
configuration during compatibility loading; the migration writer emits the new
registry section once populated. Current migration covers the primary N1/N2/oil
pressure inputs and the main fuel, starter, oil pump, cooling fan, bleed valve,
and oil-scavenge outputs within the fixed registry budget. If a legacy
closed-loop oil controller has both an oil-pressure input and oil-pump output,
loading creates a deterministic `main_oil_loop` binding.
