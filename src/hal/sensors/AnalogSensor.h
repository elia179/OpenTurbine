#pragma once
#include "ISensor.h"
#include <Arduino.h>

// ============================================================
//  Analog sensor variants — all share an ADC rolling average
//  buffer, then apply different calibration to get a value.
//
//  Uses analogRead() (raw 12-bit counts, 0–4095).
//  Calibration coefficients must be derived from raw counts.
//  ADC1 channels only: GPIO 32-39. ADC2 conflicts with WiFi.
//
//  Three variants:
//    AnalogPolySensor    — cubic polynomial cal (oil pressure)
//    AnalogLinearSensor  — linear two-point cal (flow, pressure)
//    AnalogThSensor      — threshold (flame detector)
// ============================================================

// ── Shared rolling average buffer ────────────────────────────
template <int N>
class RollingAvg {
public:
    void push(int v) {
        _buf[_idx] = v;
        _idx = (_idx + 1) % N;
        if (_filled < N) _filled++;
    }
    float avg() const {
        if (_filled == 0) return 0;
        long s = 0;
        for (int i = 0; i < _filled; i++) s += _buf[i];
        return (float)s / _filled;
    }
private:
    int _buf[N]  = {};
    int _idx     = 0;
    int _filled  = 0;
};

// ── Base analog sensor ────────────────────────────────────────
class AnalogBase : public ISensor {
public:
    AnalogBase(int pin, const char* sensorName)
        : _pin(pin), _name(sensorName) {}

    // Runtime-pin overload — update pin then initialise.
    void begin(int pin) {
        _pin = pin;
        begin();
    }

    void begin() override {
        analogSetAttenuation(ADC_11db); // 0–3.3 V full range
        analogReadResolution(12);       // 12-bit (0–4095), portable across ESP32 targets
    }

    void update() override {
        // No pin assigned (begin() was pin-gated) — analogRead(-1) would
        // spam core error logs every sample and feed 0 counts into the avg.
        if (_pin < 0) return;
        unsigned long now = millis();
        if (now - _lastMs < SAMPLE_INTERVAL_MS) return;
        _lastMs = now;
        _avg.push(analogRead(_pin));    // raw 12-bit counts
    }

    int         rawCounts()  const { return (int)_avg.avg(); }
    const char* name()       override { return _name; }

protected:
    static constexpr unsigned long SAMPLE_INTERVAL_MS = 10;

    int         _pin;
    const char* _name;
    unsigned long _lastMs = 0;
    RollingAvg<16> _avg;

    // ADC rail detection — stuck at 0 or 4095 → sensor fault
    bool _railCheck() const {
        int v = (int)_avg.avg();
        return v > 10 && v < 4085;
    }
};

// ── Polynomial calibrated (e.g. oil pressure transducer) ─────
// y = a*x^3 + b*x^2 + c*x + d   (x = raw counts, y = bar or unit)
struct PolyCal {
    float a = 0, b = 0, c = 0, d = 0;
    float xMin = 0, xMax = 4095;
};

class AnalogPolySensor : public AnalogBase {
public:
    AnalogPolySensor(int pin, const char* n) : AnalogBase(pin, n) {}

    void setCal(const PolyCal& cal) { _cal = cal; }

    float getValue() override {
        float x = _avg.avg();
        x = constrain(x, _cal.xMin, _cal.xMax);
        return _cal.a*x*x*x + _cal.b*x*x + _cal.c*x + _cal.d;
    }
    bool isHealthy() override { return _railCheck(); }

private:
    PolyCal _cal;
};

// ── Linear two-point calibrated (flow, aux pressure) ─────────
struct LinearCal {
    float rawMin = 0,    rawMax = 4095;
    float valMin = 0.0f, valMax = 1.0f;
};

class AnalogLinearSensor : public AnalogBase {
public:
    AnalogLinearSensor(int pin, const char* n) : AnalogBase(pin, n) {}

    void setCal(const LinearCal& cal) { _cal = cal; }

    float getValue() override {
        float raw = _avg.avg();
        if (_cal.rawMax <= _cal.rawMin) return 0;
        // Clamp to the calibrated electrical range — outside it the line is
        // extrapolation, not measurement, so a railed or drifted input would
        // read as a plausible physical value. Matches AnalogPolySensor.
        raw = constrain(raw, _cal.rawMin, _cal.rawMax);
        float t = (raw - _cal.rawMin) / (_cal.rawMax - _cal.rawMin);
        return _cal.valMin + t * (_cal.valMax - _cal.valMin);
    }
    bool isHealthy() override { return _railCheck(); }

private:
    LinearCal _cal;
};

// ── Threshold (flame detector) ───────────────────────────────
// Returns 1.0 when raw > threshold, 0.0 otherwise.
// isHealthy() is always true (no good way to detect failure).
class AnalogThSensor : public AnalogBase {
public:
    AnalogThSensor(int pin, const char* n) : AnalogBase(pin, n) {}

    void setThreshold(int threshold) { _threshold = threshold; }

    float getValue() override {
        return (_avg.avg() > _threshold) ? 1.0f : 0.0f;
    }
    bool isHealthy() override { return true; }

    // Rail check exposed separately: railed/disconnected wiring is worth
    // surfacing on the dashboard, but a strong flame can legitimately
    // saturate the ADC, so this must not gate the safety-side
    // flameDetected logic (isHealthy stays unconditionally true).
    bool railHealthy() const { return _railCheck(); }

    int rawCounts() const { return (int)_avg.avg(); }

private:
    int _threshold = 500;
};
