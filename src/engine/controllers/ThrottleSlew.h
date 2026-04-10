#pragma once
#include "IController.h"
#include "../EngineData.h"
#include <Arduino.h>

// ============================================================
//  ThrottleSlew — rate-limiter on throttle output
//
//  Reads throttleDemand from EngineData (set by sequencer or
//  DynamicIdle), applies ramp rates, writes the slewed result
//  back to throttleDemand for actuators to consume.
//
//  Safety pullback: reduces output when approaching overspeed
//  or overtemp limits (soft-guard before safety monitor fires).
// ============================================================

class ThrottleSlew : public IController {
public:
    // Config parameters (ms to ramp 0→100%)
    float rampUpMs          = 600.0f;
    float rampDownMs        = 800.0f;
    float idleMinPct        = 8.0f;
    float idleMaxPct        = 18.0f;

    // Safety pullback thresholds (from config: RPM_LIMIT, TOT_LIMIT)
    float rpmSoftLimit      = 95000.0f;  // start pulling back at 95% of limit
    float rpmHardLimit      = 100000.0f;
    float totSoftLimit      = 700.0f;
    float totHardLimit      = 750.0f;

    void begin() override {
        reset();
        _lastMs = millis();
    }

    void tick() override {
        auto& ed     = EngineData::instance();
        unsigned long now = millis();
        float dt          = (now - _lastMs) / 1000.0f;
        _lastMs           = now;

        float target = constrain(ed.throttleDemand, 0.0f, 1.0f);

        // Safety pullback: approach overspeed
        if (ed.n1Healthy && ed.n1Rpm > rpmSoftLimit) {
            float over = (ed.n1Rpm - rpmSoftLimit) / (rpmHardLimit - rpmSoftLimit);
            target = constrain(target - over * 0.30f, 0.0f, target);
        }
        // Safety pullback: approach overtemp
        if (ed.totHealthy && ed.tot > totSoftLimit) {
            float over = (ed.tot - totSoftLimit) / (totHardLimit - totSoftLimit);
            target = constrain(target - over * 0.20f, 0.0f, target);
        }

        float maxStep;
        if (target > _current) {
            maxStep =  dt / (rampUpMs   / 1000.0f);
        } else {
            maxStep =  dt / (rampDownMs / 1000.0f);
        }
        _current = constrain(_current + constrain(target - _current, -maxStep, maxStep),
                             0.0f, 1.0f);

        ed.throttleDemand = _current;
    }

    void reset() override {
        _current = 0;
        _lastMs  = millis();
    }

private:
    float         _current = 0;
    unsigned long _lastMs  = 0;
};
