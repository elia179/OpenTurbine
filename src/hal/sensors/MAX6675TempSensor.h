#pragma once
#include "ISensor.h"
#include <max6675.h>
#include <Arduino.h>
#include <new>

// MAX6675 K-type thermocouple SPI sensor.
// Returns °C. isHealthy() = false when chip reports open-circuit
// (MAX6675 sets bit 2 of response when no thermocouple connected).
//
// Ring buffer averaging: NUM_AVG samples averaged before reporting.
// Smooths igniter electrical noise spikes that could cause false TOT faults.
//
// Runtime-pin support: pins stored separately; MAX6675 object is
// constructed in begin() via placement new (no heap allocation).
class MAX6675TempSensor : public ISensor {
public:
    MAX6675TempSensor(int clkPin, int csPin, int misoPin, const char* sensorName)
        : _clkPin(clkPin), _csPin(csPin), _misoPin(misoPin),
          _name(sensorName), _tc(nullptr) {}

    // Runtime-pin overload — update pins then reinitialise.
    void begin(int clk, int cs, int miso) {
        _clkPin  = clk;
        _csPin   = cs;
        _misoPin = miso;
        begin();
    }

    void begin() override {
        // Placement-new: construct MAX6675 into our local storage (zero heap).
        // Call destructor first so double-init (e.g. pin change + reinit) is safe
        // even if a future library version adds teardown logic.
        if (_tc) _tc->~MAX6675();
        _tc = new (_tcBuf) MAX6675(_clkPin, _csPin, _misoPin);
        // An absent converter otherwise leaves MISO floating and may appear
        // as a plausible turbine temperature.
        pinMode(_misoPin, INPUT_PULLUP);
        _filled  = 0;
        _idx     = 0;
        _temp    = 0;
        _healthy = false;
        _lastMs  = 0;
    }

    void update() override {
        if (!_tc) return;
        unsigned long now = millis();
        if (now - _lastMs < READ_INTERVAL_MS) return;
        _lastMs = now;
        float t = _tc->readCelsius();
        // MAX6675 returns NAN on open circuit; also treat 0 °C as fault —
        // a disconnected chip frequently returns exactly 0.0 before NAN.
        // MAX6675 physical range: 0 to 1023.75 °C (10-bit + sign, 0.25°C/LSB)
        // NaN = open circuit; 0 = often returned on disconnect; >1023.75 = impossible
        if (isnan(t) || t <= 0.0f || t > 1023.75f) {
            _healthy = false;
            // Retain the last value but reset filter history after a fault.
            _filled = 0;
            _idx = 0;
            return;
        }
        _healthy     = true;
        _buf[_idx]   = t;
        _idx         = (_idx + 1) % NUM_AVG;
        if (_filled < NUM_AVG) _filled++;
        // Mean over valid samples
        float sum = 0;
        for (int i = 0; i < _filled; i++) sum += _buf[i];
        _temp = sum / _filled;
    }

    float       getValue()  override { return _temp; }
    bool        isHealthy() override { return _healthy; }
    const char* name()      override { return _name; }

private:
    static constexpr unsigned long READ_INTERVAL_MS = 250; // MAX6675 min ~220 ms
    static constexpr int           NUM_AVG          = 6;   // ~1.5 s of smoothing

    int8_t      _clkPin;
    int8_t      _csPin;
    int8_t      _misoPin;
    const char* _name;

    // Placement-new storage — sizeof/alignof MAX6675 (avoids heap alloc)
    alignas(MAX6675) uint8_t _tcBuf[sizeof(MAX6675)];
    MAX6675*    _tc;

    float       _buf[NUM_AVG] = {};
    int         _idx          = 0;
    int         _filled       = 0;
    float       _temp         = 0;
    bool        _healthy      = false;
    unsigned long _lastMs     = 0;
};
