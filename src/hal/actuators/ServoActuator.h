#pragma once
#include "IActuator.h"
#include <Arduino.h>

// Servo / ESC actuator: maps 0.0-1.0 to configured microsecond pulses.
// Uses Arduino-ESP32 3.x LEDC directly to generate standard 50 Hz servo PWM.
class ServoActuator : public IActuator {
public:
    ServoActuator(int pin, int minUs, int maxUs, const char* actuatorName)
        : _pin(pin), _minUs(minUs), _maxUs(maxUs), _name(actuatorName) {}

    // Runtime-pin overload: update params then initialise.
    void begin(int pin, int minUs, int maxUs) {
        _pin   = pin;
        _minUs = minUs;
        _maxUs = maxUs;
        begin();
    }

    void begin() override {
        if (_minUs > _maxUs) { int tmp = _minUs; _minUs = _maxUs; _maxUs = tmp; }
        ledcAttach(_pin, PWM_FREQ_HZ, PWM_RES_BITS);
        _lastUs = -1;
        writePulse(_minUs); // safe low on boot
    }

    void set(float value) override {
        value = constrain(value, 0.0f, 1.0f);
        writePulse(_minUs + (int)(value * (_maxUs - _minUs)));
    }

    void off() override {
        writePulse(_minUs);
    }

    const char* name() override { return _name; }

private:
    static constexpr uint32_t PWM_FREQ_HZ = 50;
    static constexpr uint8_t  PWM_RES_BITS = 16;
    static constexpr uint32_t PWM_MAX_DUTY = (1UL << PWM_RES_BITS) - 1UL;

    void writePulse(int us) {
        us = constrain(us, _minUs, _maxUs);
        if (us == _lastUs) return;
        uint32_t duty = ((uint64_t)us * PWM_FREQ_HZ * PWM_MAX_DUTY) / 1000000ULL;
        ledcWrite(_pin, duty);
        _lastUs = us;
    }

    int         _pin;
    int         _minUs;
    int         _maxUs;
    int         _lastUs = -1;
    const char* _name;
};
