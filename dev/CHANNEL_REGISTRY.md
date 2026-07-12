# Channel registry

Configuration version 4 introduces the bounded `channel_registry` section.
It contains separate input and output inventories, each keyed by a stable,
machine-safe ID. Labels are presentation-only and must never be persisted as
references. The ESP32 registry has fixed capacities of 16 inputs, 16 outputs,
and 16 bindings.

Output demand is normalized to `0.0..1.0`. Relay drivers quantize at `0.5` at
the physical boundary; PWM and servo drivers preserve intermediate values.
Every output has explicit boot-safe and fault-safe demands.

The parser rejects duplicate or illegal IDs, direction/driver mismatches,
invalid demand ranges, conflicting input/output pins, overflowing capacities,
and bindings to nonexistent channels. Legacy files without a registry retain
their existing singleton hardware configuration during compatibility loading;
the migration writer emits the new registry section once populated.
