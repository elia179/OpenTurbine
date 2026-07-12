#pragma once
#include "IController.h"
#include "../EngineData.h"
#include <Arduino.h>

// ============================================================
//  OilPressureLoop — integrating pressure regulator for the oil pump
//
//  Reads oilPressure from EngineData, drives oilPumpPct.
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
    float adjustScale      = 1.80f;
    float minPct           = 18.0f;
    float maxPct           = 100.0f;
    int   failsafeDelayMs  = 1500;
    float failsafePct      = 60.0f;
    float deadband         = 0.2f;  // bar: no output change when |error| < deadband

    void begin() override {
        reset();
    }

    void tick() override {
        auto& ed = EngineData::instance();

        // Bench mode: no real oil sensor or pump — skip entirely so we don't
        // trigger the failsafe timer and spam the UI with oilFailsafeActive.
        if (ed.benchMode) return;

        // dt for loop-rate-independent accumulation below. Measured before the
        // failsafe path so the stamp never goes stale on recovery.
        unsigned long now = millis();
        float dt = (now - _lastMs) / 1000.0f;
        _lastMs  = now;
        if (dt <= 0.0f || dt > 0.5f) dt = 1.0f / kNominalHz;

        if (!ed.oilHealthy) {
            // Sensor fault path — explicit armed flag, not a 0-timestamp
            // sentinel (millis() can legitimately be 0 at boot/rollover).
            if (!_failsafeArmed) {
                _failsafeArmed = true;
                _failsafeTimer = now;
            } else if ((now - _failsafeTimer) > (unsigned long)failsafeDelayMs) {
                // Switch to fixed open-loop
                ed.oilFailsafeActive = true;
                ed.oilPumpPct      = constrain(failsafePct, minPct, maxPct);
            }
            return;
        }

        bool recoveredFromFailsafe = ed.oilFailsafeActive;
        _failsafeArmed     = false;
        if (recoveredFromFailsafe) {
            _outputPct = constrain(ed.oilPumpPct, minPct, 100.0f);
        }
        ed.oilFailsafeActive = false;

        float error = ed.oilTargetBar - ed.oilPressure;
        // Deadband: only adjust when error is significant (prevents hunting).
        // Integral-style action: the output accumulates while error persists,
        // dt-normalised to the historical 400 Hz tuning rate so adjustScale
        // behaves the same at any control_loop_hz (same fix as
        // PowerTurbineGovernor).  Windup is bounded by the constrain() below;
        // recovery from saturation is immediate once error reverses sign.
        if (fabsf(error) > deadband) {
            float adj  = error * adjustScale * (dt * kNominalHz);
                _outputPct = constrain(_outputPct + adj, minPct, maxPct);
        }
        ed.oilPumpPct = _outputPct;
    }

    void reset() override {
        // Seed from current demand so the loop continues smoothly at the
        // STARTUP→RUNNING transition — avoids a one-frame duty-zero blip.
        auto& ed = EngineData::instance();
        _outputPct     = constrain(ed.oilPumpPct, minPct, maxPct);
        _failsafeArmed = false;
        _lastMs        = millis();
        ed.oilFailsafeActive = false;
    }

private:
    // Loop rate adjustScale was historically tuned against (default
    // control_loop_hz); dt-scaling is normalised to it to keep old tunings.
    static constexpr float kNominalHz = 400.0f;

    float         _outputPct     = 0;
    unsigned long _failsafeTimer = 0;
    bool          _failsafeArmed = false;
    unsigned long _lastMs        = 0;
};
