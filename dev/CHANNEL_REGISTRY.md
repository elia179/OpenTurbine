# Channel registry

Configuration version 4 introduces the bounded `channel_registry` section.
It contains separate input and output inventories, each keyed by a stable,
machine-safe ID. Labels are presentation-only and must never be persisted as
references. IDs are bounded to 19 visible characters plus the null terminator.
The classic ESP32 registry has fixed capacities of 6 inputs, 6 outputs, and 5
bindings; these bounds protect the existing static-RAM budget.

The hardware section also accepts up to two fixed-capacity `oil_loops`
definitions. Each loop persists a stable loop ID plus `pressure_input` and
`pump_output` channel IDs. Loading resolves those IDs to compact registry
indexes; the first enabled loop feeds the existing single oil-pressure
controller compatibility path, including min/max demand and deadband. Additional
enabled loops are validated for unique pump ownership, but independent runtime
multi-pump control still requires the generic output dispatch layer.

Output demand is normalized to `0.0..1.0`. Relay drivers quantize at `0.5` at
the physical boundary; PWM and servo drivers preserve intermediate values.
Every output has explicit boot-safe and fault-safe demands.

The parser rejects duplicate or illegal IDs, direction/driver mismatches,
invalid demand ranges, conflicting input/output pins, overflowing capacities,
bindings to nonexistent channels, and wrong-direction standard bindings such
as an output bound to `primary_n1`.

Control rules, sequence side actions, and custom sequence blocks now serialize
stable `source` / `target` IDs and resolve them once at load to compact runtime
handles. Legacy numeric fields remain readable for old files. Generic channel
runtime I/O is still behind the compatibility adapter layer: stable IDs,
standard binding keys such as `primary_n1` / `main_fuel_output`, and registry
channel roles resolve onto the built-in singleton hardware paths where a safe
compatibility mapping exists. Fully generic input reads and output writes still
need the physical-driver dispatch layer to become the next authority.

Legacy files without a registry retain their existing singleton hardware
configuration during compatibility loading; the migration writer emits the new
registry section once populated. Current migration covers the primary N1/N2/oil
pressure inputs and the main fuel, starter, oil pump, cooling fan, bleed valve,
and oil-scavenge outputs within the fixed registry budget. If a legacy
closed-loop oil controller has both an oil-pressure input and oil-pump output,
loading creates a deterministic `main_oil_loop` binding.
