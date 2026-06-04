#pragma once
#include "AnalogSensor.h"
#include <math.h>

// ============================================================
//  NTCSensor — NTC thermistor temperature sensor
//
//  Uses the simplified B-parameter (Steinhart-Hart) equation:
//    1/T = 1/T0 + (1/B) * ln(R/R0)
//
//  Where:
//    T   = temperature in Kelvin
//    T0  = reference temperature in Kelvin (default 25°C = 298.15 K)
//    R0  = thermistor resistance at T0 (default 10 kΩ)
//    B   = thermistor beta coefficient (default 3950, typical for
//          standard 10 kΩ NTC used in hobby electronics)
//    R   = measured thermistor resistance
//
//  Circuit assumes a voltage divider:
//    3V3 ── R_fixed ──┬── NTC ── GND
//                    ADC
//  So: R_ntc = rFixed * raw / (4095 - raw)
//
//  Config params (set before begin(), or leave as defaults):
//    rFixed  — fixed pull-up resistor in Ω (default 10000)
//    r0      — NTC resistance at reference temp in Ω (default 10000)
//    t0C     — reference temperature in °C (default 25)
//    beta    — B coefficient (default 3950)
//
//  isHealthy() requires at least one valid reading and raw not railed.
// ============================================================

struct NTCCal {
    float rFixed = 10000.0f;   // pull-up resistor (Ω)
    float r0     = 10000.0f;   // NTC resistance at reference temp (Ω)
    float t0C    = 25.0f;      // reference temperature (°C)
    float beta   = 3950.0f;    // B coefficient
};

class NTCSensor : public AnalogBase {
public:
    NTCSensor(int pin, const char* n) : AnalogBase(pin, n) {}

    void setCal(const NTCCal& cal) { _cal = cal; }

    float getValue() override {
        if (!_calValid()) return -999.0f;
        float raw = _avg.avg();
        // Avoid divide-by-zero at rails
        if (raw <= 0.0f || raw >= 4095.0f) return -999.0f;

        float r = _cal.rFixed * raw / (4095.0f - raw);
        if (r <= 0.0f) return -999.0f;

        float t0K = _cal.t0C + 273.15f;
        float invT = (1.0f / t0K) + (1.0f / _cal.beta) * logf(r / _cal.r0);
        return (1.0f / invT) - 273.15f;   // Kelvin → °C
    }

    bool isHealthy() override {
        float value = getValue();
        return _calValid() && _railCheck() && _avg.avg() > 0 && isfinite(value);
    }

private:
    bool _calValid() const {
        return _cal.rFixed > 0.0f && _cal.r0 > 0.0f &&
               _cal.beta > 0.0f && _cal.t0C > -273.15f;
    }

    NTCCal _cal;
};
