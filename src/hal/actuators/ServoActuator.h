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
        // ledcAttach only succeeds when 2^bits <= clk/freq. The ESP32-S3's LEDC
        // clock is lower than the classic ESP32's, so 50 Hz @ 16-bit FAILS to
        // attach there — leaving the servo/ESC pin dead and the throttle with no
        // signal (LEDCActuator hits the same wall and handles it the same way).
        // Retry at progressively lower resolution — a servo only needs a few
        // thousand steps across its 1-2 ms band — and track the resolution
        // actually used so the duty scaling stays correct.
        _resBits = MAX_RES_BITS;
        _maxDuty = (1UL << _resBits) - 1UL;
        bool ok = _pin >= 0 && ledcAttach(_pin, PWM_FREQ_HZ, _resBits);
        while (!ok && _resBits > MIN_RES_BITS) {
            _resBits--;
            _maxDuty = (1UL << _resBits) - 1UL;
            ok = ledcAttach(_pin, PWM_FREQ_HZ, _resBits);
        }
        Serial.printf("[%s] servo attach pin=%d freq=%uHz bits=%u %s\n",
                      _name, _pin, (unsigned)PWM_FREQ_HZ, (unsigned)_resBits,
                      ok ? "OK" : "FAILED");
        _ready = ok;
        _lastUs = -1;
        if (_ready) writePulse(_minUs); // safe low on boot
    }

    void set(float value) override {
        value = constrain(value, 0.0f, 1.0f);
        writePulse(_minUs + (int)(value * (_maxUs - _minUs)));
    }

    void off() override {
        writePulse(_minUs);
    }

    const char* name() override { return _name; }
    bool isReady() const override { return _ready; }

private:
    static constexpr uint32_t PWM_FREQ_HZ  = 50;
    static constexpr uint8_t  MAX_RES_BITS = 16;
    // Floor for the attach-retry. At 50 Hz a 12-bit timer still gives ~4.9 us
    // duty steps (~200 steps across a 1000 us servo band) — plenty for an ESC.
    static constexpr uint8_t  MIN_RES_BITS = 12;

    void writePulse(int us) {
        if (!_ready) return;
        us = constrain(us, _minUs, _maxUs);
        if (us == _lastUs) return;
        uint32_t duty = ((uint64_t)us * PWM_FREQ_HZ * _maxDuty) / 1000000ULL;
        ledcWrite(_pin, duty);
        _lastUs = us;
    }

    int         _pin;
    int         _minUs;
    int         _maxUs;
    int         _lastUs = -1;
    const char* _name;
    uint8_t     _resBits = MAX_RES_BITS;
    uint32_t    _maxDuty = (1UL << MAX_RES_BITS) - 1UL;
    bool        _ready = false;
};
