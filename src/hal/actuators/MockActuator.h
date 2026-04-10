#pragma once
#include "IActuator.h"

// DEV_MODE only — logs calls, does nothing physical.
class MockActuator : public IActuator {
public:
    explicit MockActuator(const char* actuatorName) : _name(actuatorName) {}

    void begin()          override {}
    void set(float value) override { _lastValue = value; }
    void off()            override { _lastValue = 0; }
    const char* name()    override { return _name; }

    float lastValue() const { return _lastValue; }

private:
    const char* _name;
    float       _lastValue = 0;
};
