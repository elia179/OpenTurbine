#pragma once
#include "IActuator.h"
#include <Arduino.h>

// ============================================================
//  LEDCActuator — high-frequency PWM via ESP32 LEDC hardware unit
//
//  Uses Arduino-ESP32 3.x simplified LEDC API:
//    ledcAttach(pin, freq, resolution) → attach and configure
//    ledcWrite(pin, duty)              → set duty cycle
//    ledcDetach(pin)                   → release
//
//  Typical use: oil pump BLDC, cooling fans.
//  Default: 10 kHz, 12-bit resolution (0–4095 duty).
// ============================================================

class LEDCActuator : public IActuator {
public:
    LEDCActuator(int pin, uint32_t freqHz, uint8_t resBits, const char* actuatorName)
        : _pin(pin), _freqHz(freqHz), _resBits(resBits), _name(actuatorName) {
        _maxDuty = (1u << resBits) - 1;
    }

    // Runtime-pin overload — update params then initialise.
    void begin(int pin, uint32_t freqHz, uint8_t resBits) {
        _pin     = pin;
        _freqHz  = freqHz;
        _resBits = resBits;
        _maxDuty = (1u << resBits) - 1;
        begin();
    }

    // Invert output: 0% demand → full duty, 100% demand → 0 duty.
    // Useful for active-low motor controllers or inverted ESC signal conventions.
    void setInverted(bool inv) { _inverted = inv; }

    void setOutputRange(float minPct, float maxPct) {
        minPct = constrain(minPct, 0.0f, 100.0f);
        maxPct = constrain(maxPct, 0.0f, 100.0f);
        if (maxPct < minPct) {
            float t = minPct;
            minPct = maxPct;
            maxPct = t;
        }
        _minOutput = minPct / 100.0f;
        _maxOutput = maxPct / 100.0f;
    }

    void begin() override {
        // A given (frequency, resolution) pair is only achievable if the LEDC
        // timer clock can divide finely enough: resolution_bits must satisfy
        // 2^bits <= clk/freq. The ESP32-S3 selects a lower LEDC clock than the
        // classic ESP32, so e.g. 10 kHz @ 12-bit (needs a >4096 divider) fails
        // there with "div_param=0" while the classic's 80 MHz clock succeeds.
        // Retry at progressively lower resolution so the output still works
        // with slightly coarser duty steps instead of not attaching at all.
        // _maxDuty and the P-controller's maxDuty() track the resolution
        // actually used, so duty scaling stays correct.
        bool ok = ledcAttach(_pin, _freqHz, _resBits);
        while (!ok && _resBits > MIN_RES_BITS) {
            _resBits--;
            _maxDuty = (1u << _resBits) - 1;
            ok = ledcAttach(_pin, _freqHz, _resBits);
        }
        Serial.printf("[%s] LEDC attach pin=%d freq=%luHz bits=%u %s\n",
                      _name, _pin, (unsigned long)_freqHz, (unsigned)_resBits,
                      ok ? "OK" : "FAILED");
        _lastDuty = NO_DUTY;
        writeDuty(_inverted ? _maxDuty : 0);  // off on boot
    }

    void set(float value) override {
        value = constrain(value, 0.0f, 1.0f);
        if (value <= 0.0f) {
            off();
            return;
        }
        value = _minOutput + value * (_maxOutput - _minOutput);
        if (_inverted) value = 1.0f - value;
        uint32_t duty = (uint32_t)(value * _maxDuty);
        writeDuty(duty);
    }

    void off() override {
        writeDuty(_inverted ? _maxDuty : 0);
    }

    const char* name() override { return _name; }

    // Raw duty access for P-controller that works in counts
    void setDuty(uint32_t duty) {
        writeDuty((uint32_t)constrain((int)duty, 0, (int)_maxDuty));
    }

    uint32_t maxDuty() const { return _maxDuty; }

private:
    static constexpr uint32_t NO_DUTY = UINT32_MAX;
    // Floor for the attach-retry: 8-bit (256 steps) still gives usable motor
    // control; below this a PWM output isn't worth keeping.
    static constexpr uint8_t  MIN_RES_BITS = 8;

    void writeDuty(uint32_t duty) {
        duty = (uint32_t)constrain((int)duty, 0, (int)_maxDuty);
        if (duty == _lastDuty) return;

        // Log only on transitions into/out of 0% and 100% duty. set() is called
        // every loop tick, so a level-based condition here would print
        // continuously while an output sits at full duty (e.g. oil prime).
        if (_lastDuty != NO_DUTY &&
            ((duty == 0) != (_lastDuty == 0) ||
             (duty == _maxDuty) != (_lastDuty == _maxDuty))) {
            Serial.printf("[%s] LEDC duty pin=%d duty=%lu/%lu\n",
                          _name, _pin, (unsigned long)duty, (unsigned long)_maxDuty);
        }

        ledcWrite(_pin, duty);
        _lastDuty = duty;
    }

    int         _pin;
    uint32_t    _freqHz;
    uint8_t     _resBits;
    const char* _name;
    uint32_t    _maxDuty;
    uint32_t    _lastDuty = NO_DUTY;
    bool        _inverted = false;
    float       _minOutput = 0.0f;
    float       _maxOutput = 1.0f;
};
