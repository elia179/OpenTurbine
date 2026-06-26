#pragma once
#include "IActuator.h"
#include <Arduino.h>

// Digital relay / MOSFET actuator.
// set(v >= 0.5) → on, set(v < 0.5) → off.
// activeHigh = true for active-high logic (most MOSFETs);
// activeHigh = false for active-low relay boards.
class RelayActuator : public IActuator {
public:
    RelayActuator(int pin, bool activeHigh, const char* actuatorName)
        : _pin(pin), _activeHigh(activeHigh), _name(actuatorName) {}

    // Runtime-pin overload — update params then initialise.
    void begin(int pin, bool activeHigh) {
        _pin        = pin;
        _activeHigh = activeHigh;
        begin();
    }

    void begin() override {
        pinMode(_pin, OUTPUT);
        _hasLast = false;
        off();
    }

    void set(float value) override {
        _write(value >= 0.5f);
    }

    void off() override {
        _write(false);
    }

    const char* name() override { return _name; }

    // Convenience for boolean callers
    void setOn(bool on) { _write(on); }

private:
    void _write(bool on) {
        if (_hasLast && on == _lastOn) return;
        digitalWrite(_pin, (_activeHigh ? on : !on) ? HIGH : LOW);
        _lastOn = on;
        _hasLast = true;
    }

    int         _pin;
    bool        _activeHigh;
    bool        _lastOn = false;
    bool        _hasLast = false;
    const char* _name;
};
