# Channel registry

Configuration version 4 introduces the bounded `channel_registry` section.
It contains separate input and output inventories, each keyed by a stable,
machine-safe ID. Labels are presentation-only and must never be persisted as
references. The classic ESP32 registry has fixed capacities of 6 inputs, 6
outputs, and 5 bindings; these bounds protect the existing static-RAM budget.

Output demand is normalized to `0.0..1.0`. Relay drivers quantize at `0.5` at
the physical boundary; PWM and servo drivers preserve intermediate values.
Every output has explicit boot-safe and fault-safe demands.

The parser rejects duplicate or illegal IDs, direction/driver mismatches,
invalid demand ranges, conflicting input/output pins, overflowing capacities,
and bindings to nonexistent channels. Legacy files without a registry retain
their existing singleton hardware configuration during compatibility loading;
the migration writer emits the new registry section once populated.
