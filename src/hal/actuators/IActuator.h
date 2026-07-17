#pragma once

// ── Actuator interface ────────────────────────────────────────
// All actuators implement this. Controllers write normalized
// demands (0.0–1.0) to EngineData; the ECU loop calls set()
// explicitly. Actuators never read EngineData.
class IActuator {
public:
    virtual ~IActuator() = default;
    virtual void        begin()           = 0;
    virtual void        set(float value)  = 0;  // 0.0 = off/min, 1.0 = on/max
    virtual void        off()             = 0;  // immediate safe-off
    virtual const char* name()            = 0;
    virtual bool        isReady() const    = 0;
};
