#pragma once
#include "../../engine/EngineData.h"
#include "../../engine/Types.h"
#include "../../system/HardwareConfig.h"
#include "hardware_profile.h"
#include <Arduino.h>
#if defined(OT_PLATFORM_ESP32S3)
#include "esp32-hal-rgb-led.h"
#endif

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
static constexpr int AUTO_S3_RGB_STATUS_LED_PIN = -2;
static constexpr uint8_t S3_RGB_LEVEL = 64;

static bool _isRgbStatusLed(int pin) {
#if defined(OT_PLATFORM_ESP32S3)
    return HardwareConfig::statusLedType == 1 && pin == 48;
#else
    (void)pin;
    return false;
#endif
}

static int _statusPin() {
    int pin = HardwareConfig::statusLedPin;
#if defined(OT_PLATFORM_ESP32S3)
    if (pin == AUTO_S3_RGB_STATUS_LED_PIN || pin == 38) return 48;
#endif
    return pin;
}

static void _writeLed(int pin, bool on) {
#if defined(OT_PLATFORM_ESP32S3)
    if (_isRgbStatusLed(pin)) {
        const uint8_t level = on ? S3_RGB_LEVEL : 0;
        rgbLedWrite((uint8_t)pin, 0, level, 0);
        return;
    }
#endif
    digitalWrite(pin, on ? HIGH : LOW);
}

static void _writeStatusLed(bool on) {
    int pin = _statusPin();
    if (pin >= 0) _writeLed(pin, on);
}

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
    int pin = _statusPin();
    if (pin < 0) return;
#if defined(OT_PLATFORM_ESP32S3)
    if (!_isRgbStatusLed(pin))
#endif
        pinMode(pin, OUTPUT);
    _writeStatusLed(false);
    _nextMs          = millis();
    _blinksRemaining = 0;
    _inPause         = true;
    _ledOn           = false;
#if defined(OT_PLATFORM_ESP32S3)
    if (_isRgbStatusLed(pin)) {
        _ledOn = true;
        _blinksRemaining = 1;
        _inPause = false;
        _writeStatusLed(true);
        _nextMs = millis() + 300;
    }
#endif
}

inline void tick() {
    int pin = _statusPin();
    if (pin < 0) return;
    unsigned long now = millis();
    if (now < _nextMs) return;

    SysMode mode = EngineData::instance().mode;

    if (mode == SysMode::FAULT) {
        _ledOn = !_ledOn;
        _writeStatusLed(_ledOn);
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
            _writeStatusLed(true);
            _nextMs = now + BLINK_ON_MS;
        } else {
            _ledOn = false;
            _writeStatusLed(false);
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
