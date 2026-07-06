#pragma once
#include "ISensor.h"
#include <Arduino.h>

// Minimal non-blocking HX711 reader for torque load cells, channel A/gain 128.
class HX711Sensor : public ISensor {
public:
    HX711Sensor(int doutPin, int clockPin, const char* sensorName)
        : _doutPin(doutPin), _clockPin(clockPin), _name(sensorName) {}

    void begin(int doutPin, int clockPin, float nmPerCount, long zeroCount) {
        _doutPin = doutPin;
        _clockPin = clockPin;
        _scale = nmPerCount;
        _zero = zeroCount;
        begin();
    }

    void begin() override {
        if (_doutPin < 0 || _clockPin < 0) return;
        // Pull-up: a real HX711 drives DOUT push-pull (idle HIGH between
        // samples); without a converter the pin would float and could read
        // LOW, clocking in 24 bits of noise as a healthy torque reading.
        pinMode(_doutPin, INPUT_PULLUP);
        pinMode(_clockPin, OUTPUT);
        digitalWrite(_clockPin, LOW);
        _healthy = false;
    }

    void update() override {
        if (_doutPin < 0 || _clockPin < 0 || digitalRead(_doutPin) != LOW) {
            if (_lastSampleMs && millis() - _lastSampleMs > 1000) _healthy = false;
            return;
        }

        uint32_t data = 0;
        noInterrupts();
        for (int i = 0; i < 24; i++) {
            digitalWrite(_clockPin, HIGH);
            delayMicroseconds(1);
            data = (data << 1) | (digitalRead(_doutPin) ? 1u : 0u);
            digitalWrite(_clockPin, LOW);
            delayMicroseconds(1);
        }
        digitalWrite(_clockPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(_clockPin, LOW);
        interrupts();

        if (data & 0x800000u) data |= 0xFF000000u;
        _raw = (long)((int32_t)data);
        _value = (float)(_raw - _zero) * _scale;
        _lastSampleMs = millis();
        _healthy = true;
    }

    float getValue() override { return _value; }
    bool isHealthy() override { return _healthy; }
    const char* name() override { return _name; }
    long rawCounts() const { return _raw; }

private:
    int _doutPin;
    int _clockPin;
    const char* _name;
    float _scale = 1.0f;
    long _zero = 0;
    long _raw = 0;
    float _value = 0.0f;
    unsigned long _lastSampleMs = 0;
    bool _healthy = false;
};
