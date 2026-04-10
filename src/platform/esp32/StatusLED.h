#pragma once
#include "../../engine/EngineData.h"
#include "../../engine/Types.h"
#include "../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  StatusLED — mode-driven blink indicator
//
//  Pin is set at runtime via HardwareConfig::statusLedPin.
//  Only active when HardwareConfig::hasStatusLed == true and
//  statusLedPin >= 0.
//
//  Blink patterns:
//    STANDBY  — 1 blink  per cycle
//    STARTUP  — 2 blinks per cycle
//    RUNNING  — 3 blinks per cycle
//    SHUTDOWN — 4 blinks per cycle
//    FAULT    — rapid continuous 100 ms flash
//
//  Call StatusLED::begin() in setup(), StatusLED::tick() in loop().
//  tick() is non-blocking (millis-based state machine).
// ============================================================

namespace StatusLED {

// ── Config ────────────────────────────────────────────────────
static constexpr unsigned long BLINK_ON_MS    = 80;
static constexpr unsigned long BLINK_OFF_MS   = 120;
static constexpr unsigned long BURST_PAUSE_MS = 1500;
static constexpr unsigned long FAULT_PERIOD_MS= 100;

// ── State ─────────────────────────────────────────────────────
static int           _blinksRemaining = 0;
static bool          _ledOn           = false;
static unsigned long _nextMs          = 0;
static bool          _inPause         = false;

static int _blinksForMode(SysMode m) {
    switch (m) {
        case SysMode::STANDBY:  return 1;
        case SysMode::STARTUP:  return 2;
        case SysMode::RUNNING:  return 3;
        case SysMode::SHUTDOWN: return 4;
        case SysMode::FAULT:    return -1;
        default:                return 1;
    }
}

inline void begin() {
    int pin = HardwareConfig::statusLedPin;
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    _nextMs          = millis() + BURST_PAUSE_MS;
    _blinksRemaining = 0;
    _inPause         = true;
    _ledOn           = false;
}

inline void tick() {
    int pin = HardwareConfig::statusLedPin;
    if (pin < 0) return;
    unsigned long now = millis();
    if (now < _nextMs) return;

    SysMode mode = EngineData::instance().mode;

    if (mode == SysMode::FAULT) {
        _ledOn = !_ledOn;
        digitalWrite(pin, _ledOn ? HIGH : LOW);
        _nextMs = now + FAULT_PERIOD_MS;
        return;
    }

    int target = _blinksForMode(mode);

    if (_inPause) {
        _blinksRemaining = target;
        _inPause         = false;
    }

    if (_blinksRemaining > 0) {
        if (!_ledOn) {
            _ledOn = true;
            digitalWrite(pin, HIGH);
            _nextMs = now + BLINK_ON_MS;
        } else {
            _ledOn = false;
            digitalWrite(pin, LOW);
            _blinksRemaining--;
            if (_blinksRemaining > 0) {
                _nextMs = now + BLINK_OFF_MS;
            } else {
                _nextMs  = now + BURST_PAUSE_MS;
                _inPause = true;
            }
        }
    }
}

} // namespace StatusLED
