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
        _healthy = false;
        _lastSampleMs = 0;
        _startedMs = millis();
        _saturatedSamples = 0;
        _medianCount = 0;
        _medianIndex = 0;
        _sampleSeq = 0;
        if (_doutPin < 0 || _clockPin < 0) return;
        // Pull-up: a real HX711 drives DOUT push-pull (idle HIGH between
        // samples); without a converter the pin would float and could read
        // LOW, clocking in 24 bits of noise as a healthy torque reading.
        pinMode(_doutPin, INPUT_PULLUP);
        pinMode(_clockPin, OUTPUT);
        digitalWrite(_clockPin, LOW);
    }

    void update() override {
        if (_doutPin < 0 || _clockPin < 0 || digitalRead(_doutPin) != LOW) {
            if (_lastSampleMs && millis() - _lastSampleMs > STALE_TIMEOUT_MS) _healthy = false;
            return;
        }

        uint32_t data = 0;
        noInterrupts();
        for (int i = 0; i < 24; i++) {
            digitalWrite(_clockPin, HIGH);
            delayMicroseconds(CLOCK_HALF_PERIOD_US);
            data = (data << 1) | (digitalRead(_doutPin) ? 1u : 0u);
            digitalWrite(_clockPin, LOW);
            delayMicroseconds(CLOCK_HALF_PERIOD_US);
        }
        digitalWrite(_clockPin, HIGH);
        delayMicroseconds(CLOCK_HALF_PERIOD_US);
        digitalWrite(_clockPin, LOW);
        interrupts();

        const unsigned long now = millis();
        if (now - _startedMs < STARTUP_SETTLE_MS) {
            _healthy = false;
            return;
        }

        const uint32_t raw24 = data & 0xFFFFFFu;
        if (raw24 == 0x7FFFFFu || raw24 == 0x800000u) {
            if (_saturatedSamples < 255) ++_saturatedSamples;
            if (_saturatedSamples >= SATURATION_LIMIT) _healthy = false;
            return;
        }
        _saturatedSamples = 0;

        if (data & 0x800000u) data |= 0xFF000000u;
        const long sample = (long)((int32_t)data);
        _median[_medianIndex] = sample;
        _medianIndex = (_medianIndex + 1) % 3;
        if (_medianCount < 3) ++_medianCount;
        _raw = _medianCount < 3 ? sample : _median3(_median[0], _median[1], _median[2]);
        _value = (float)(_raw - _zero) * _scale;
        _lastSampleMs = now;
        _healthy = true;
        ++_sampleSeq;
    }

    float getValue() override { return _value; }
    bool isHealthy() override { return _healthy; }
    const char* name() override { return _name; }
    uint32_t sampleSequence() override { return _sampleSeq; }
    uint32_t sampleTimestampMs() override { return _lastSampleMs; }
    long rawCounts() const { return _raw; }

private:
    static constexpr unsigned long STARTUP_SETTLE_MS = 400;
    static constexpr unsigned long STALE_TIMEOUT_MS = 500;
    static constexpr uint8_t SATURATION_LIMIT = 3;
    // 100 kHz remains comfortably within HX711 timing limits while leaving
    // margin for optocouplers, level shifters, and long bench wiring.
    static constexpr unsigned int CLOCK_HALF_PERIOD_US = 5;

    static long _median3(long a, long b, long c) {
        if (a > b) { long t = a; a = b; b = t; }
        if (b > c) { long t = b; b = c; c = t; }
        if (a > b) { long t = a; a = b; b = t; }
        return b;
    }

    int _doutPin;
    int _clockPin;
    const char* _name;
    float _scale = 1.0f;
    long _zero = 0;
    long _raw = 0;
    float _value = 0.0f;
    unsigned long _lastSampleMs = 0;
    unsigned long _startedMs = 0;
    long _median[3] = {};
    uint8_t _medianCount = 0;
    uint8_t _medianIndex = 0;
    uint8_t _saturatedSamples = 0;
    uint32_t _sampleSeq = 0;
    bool _healthy = false;
};
