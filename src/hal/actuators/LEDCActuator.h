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

    void begin() override {
        bool ok = ledcAttach(_pin, _freqHz, _resBits);
        Serial.printf("[%s] LEDC attach pin=%d freq=%luHz bits=%u %s\n",
                      _name, _pin, (unsigned long)_freqHz, (unsigned)_resBits,
                      ok ? "OK" : "FAILED");
        ledcWrite(_pin, _inverted ? _maxDuty : 0);  // off on boot
    }

    void set(float value) override {
        value = constrain(value, 0.0f, 1.0f);
        if (_inverted) value = 1.0f - value;
        uint32_t duty = (uint32_t)(value * _maxDuty);
        // Log only on transitions into/out of 0% and 100% duty. set() is called
        // every loop tick, so a level-based condition here would print
        // continuously while an output sits at full duty (e.g. oil prime) —
        // Serial blocks once its TX buffer fills, adding jitter to the control loop.
        if ((duty == 0) != (_lastDuty == 0) || (duty == _maxDuty) != (_lastDuty == _maxDuty)) {
            Serial.printf("[%s] LEDC duty pin=%d duty=%lu/%lu\n",
                          _name, _pin, (unsigned long)duty, (unsigned long)_maxDuty);
        }
        _lastDuty = duty;
        ledcWrite(_pin, duty);
    }

    void off() override {
        ledcWrite(_pin, _inverted ? _maxDuty : 0);
    }

    const char* name() override { return _name; }

    // Raw duty access for P-controller that works in counts
    void setDuty(uint32_t duty) {
        ledcWrite(_pin, constrain((int)duty, 0, (int)_maxDuty));
    }

    uint32_t maxDuty() const { return _maxDuty; }

private:
    int         _pin;
    uint32_t    _freqHz;
    uint8_t     _resBits;
    const char* _name;
    uint32_t    _maxDuty;
    uint32_t    _lastDuty = 0;
    bool        _inverted = false;
};
