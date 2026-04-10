#pragma once
#include "IController.h"
#include "../EngineData.h"
#include <Arduino.h>

// ============================================================
//  OilPressureLoop — P-controller for oil pressure
//
//  Reads oilPressure from EngineData, drives oilPctDemand.
//  The actual actuator set() call is done by Hardware::updateActuators().
//
//  Failsafe: if sensor goes unhealthy mid-run for > OIL_FAILSAFE_DELAY_MS,
//  switches to fixed open-loop duty (from config) and sets oilFailsafeActive.
//
//  All gains and thresholds come from Config (loaded at runtime).
// ============================================================

class OilPressureLoop : public IController {
public:
    // Config parameters — populated from Config before begin()
    float targetBar        = 3.8f;
    float adjustScale      = 1.80f;
    float minPct           = 18.0f;
    int   failsafeDelayMs  = 1500;
    float failsafePct      = 60.0f;
    float deadband         = 0.2f;  // bar: no output change when |error| < deadband

    void begin() override {
        reset();
    }

    void tick() override {
        auto& ed = EngineData::instance();

        if (!ed.oilHealthy) {
            // Sensor fault path
            if (_failsafeTimer == 0) {
                _failsafeTimer = millis();
            } else if ((millis() - _failsafeTimer) > (unsigned long)failsafeDelayMs) {
                // Switch to fixed open-loop
                ed.oilFailsafeActive = true;
                ed.oilPctDemand      = failsafePct;
            }
            return;
        }

        _failsafeTimer     = 0;
        ed.oilFailsafeActive = false;

        float error = ed.oilDemand - ed.oilPressure;
        // Deadband: only adjust when error is significant (prevents hunting).
        // This is a pure P-controller — there is no integral term, so there is
        // no integrator windup to guard against.  Output saturation is handled
        // by the constrain() call below; recovery from saturation is immediate
        // once error reverses sign.
        if (fabsf(error) > deadband) {
            float adj  = error * adjustScale;
            _outputPct = constrain(_outputPct + adj, minPct, 100.0f);
        }
        ed.oilPctDemand = _outputPct;
    }

    void reset() override {
        // Seed from current demand so the P-loop continues smoothly at the
        // STARTUP→RUNNING transition — avoids a one-frame duty-zero blip.
        auto& ed = EngineData::instance();
        _outputPct     = constrain(ed.oilPctDemand, minPct, 100.0f);
        _failsafeTimer = 0;
        ed.oilFailsafeActive = false;
    }

private:
    float         _outputPct     = 0;
    unsigned long _failsafeTimer = 0;
};
