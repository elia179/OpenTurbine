#pragma once
#include <stdint.h>

// ── Sensor interface ─────────────────────────────────────────
// All sensors implement this. The ECU loop calls update() every
// tick, then reads getValue() and isHealthy() into EngineData.
// Consumers never touch sensor objects directly.
class ISensor {
public:
    virtual ~ISensor() = default;
    virtual void        begin()     = 0;
    virtual void        update()    = 0;   // called every loop tick (rate-limits internally)
    virtual float       getValue()  = 0;   // last good calibrated reading
    virtual bool        isHealthy() = 0;   // false = reading not trustworthy
    virtual const char* name()      = 0;   // e.g. "N1_RPM" — for logging / web UI
    virtual uint32_t    sampleSequence() { return 0; }
    // Rate estimators consume each sequence once and use its actual timestamp.
    virtual uint32_t    sampleTimestampMs() { return 0; }
};
