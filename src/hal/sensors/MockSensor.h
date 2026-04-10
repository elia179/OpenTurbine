#pragma once
#include "ISensor.h"

// DEV_MODE only — scripted sensor that returns a fixed or ramping value.
// Useful for testing sequences without real hardware attached.
class MockSensor : public ISensor {
public:
    MockSensor(const char* sensorName, float fixedValue = 0.0f, bool healthy = true)
        : _name(sensorName), _value(fixedValue), _healthy(healthy) {}

    void begin()   override {}
    void update()  override {
        // Scripted ramp support: if _rampRate != 0, value changes per tick
        _value += _rampRate;
    }

    float       getValue()  override { return _value; }
    bool        isHealthy() override { return _healthy; }
    const char* name()      override { return _name; }

    // Runtime scripting (from web UI in DEV_MODE)
    void setValue(float v)   { _value   = v; }
    void setHealthy(bool h)  { _healthy = h; }
    void setRamp(float rate) { _rampRate = rate; }

private:
    const char* _name;
    float       _value    = 0;
    bool        _healthy  = true;
    float       _rampRate = 0;
};
