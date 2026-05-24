#pragma once
#include "IActuator.h"
#include <ESP32Servo.h>

// Servo / ESC actuator — maps 0.0–1.0 to 1000–2000 µs PWM.
// Uses ESP32Servo library (LEDC-backed, not timer conflicts).
class ServoActuator : public IActuator {
public:
    ServoActuator(int pin, int minUs, int maxUs, const char* actuatorName)
        : _pin(pin), _minUs(minUs), _maxUs(maxUs), _name(actuatorName) {}

    // Runtime-pin overload — update params then initialise.
    void begin(int pin, int minUs, int maxUs) {
        _pin   = pin;
        _minUs = minUs;
        _maxUs = maxUs;
        begin();
    }

    void begin() override {
        if (_minUs > _maxUs) { int tmp = _minUs; _minUs = _maxUs; _maxUs = tmp; }
        _servo.setPeriodHertz(50);   // 50 Hz standard servo/ESC frame rate
        _servo.attach(_pin, _minUs, _maxUs);
        _servo.writeMicroseconds(_minUs); // safe low on boot
    }

    void set(float value) override {
        value = constrain(value, 0.0f, 1.0f);
        int us = _minUs + (int)(value * (_maxUs - _minUs));
        _servo.writeMicroseconds(us);
    }

    void off() override {
        _servo.writeMicroseconds(_minUs);
    }

    const char* name() override { return _name; }

private:
    Servo       _servo;
    int         _pin;
    int         _minUs;
    int         _maxUs;
    const char* _name;
};
