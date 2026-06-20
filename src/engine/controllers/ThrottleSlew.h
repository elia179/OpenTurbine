#pragma once
#include "IController.h"
#include "../EngineData.h"
#include "../../system/Config.h"
#include "../../system/HardwareConfig.h"
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

    // Safety pullback thresholds (from config: RPM limit and selected EGT limit)
    float rpmSoftLimit      = 95000.0f;  // start pulling back at 95% of limit
    float rpmHardLimit      = 100000.0f;
    float totSoftLimit      = 700.0f;
    float totHardLimit      = 750.0f;

    void begin() override {
        // Carry forward the current throttle demand so the physical actuator
        // does not dip to zero when RUNNING is entered after the Spool block.
        // reset() still starts from 0 and is used at power-up initialisation.
        _current = constrain(EngineData::instance().throttleDemand, 0.0f, 1.0f);
        _lastMs  = millis();
    }

    void tick() override {
        auto& ed     = EngineData::instance();
        unsigned long now = millis();
        float dt          = (now - _lastMs) / 1000.0f;
        _lastMs           = now;
        if (dt <= 0.0f) dt = 0.001f;
        else if (dt > 0.05f) dt = 0.05f;

        float target = constrain(ed.throttleDemand, 0.0f, 1.0f);
        // Dry Bench Mode intentionally permits actuator travel without fitted
        // feedback. In normal operation this interlock must still prevent a
        // fuel increase with missing speed or temperature feedback.
        if (!ed.benchMode && ((HardwareConfig::hasN1Rpm && !ed.n1Healthy) ||
            (Config::effectiveEgtSource() != 0 && !Config::primaryEgtHealthy(ed)))) {
            if (target > _current) target = _current;
        }

        // Safety pullback: approach overspeed
        // Guard: rpmHardLimit must be strictly above rpmSoftLimit — equal limits
        // would cause division by zero and NaN throttle demand.
        if (ed.n1Healthy && ed.n1Rpm > rpmSoftLimit && rpmHardLimit > rpmSoftLimit) {
            float over = (ed.n1Rpm - rpmSoftLimit) / (rpmHardLimit - rpmSoftLimit);
            target = constrain(target - over * 0.30f, 0.0f, target);
        }
        // Safety pullback: approach overtemp
        if (totHardLimit > 0.0f && Config::primaryEgtHealthy(ed) && Config::primaryEgtC(ed) > totSoftLimit
            && totHardLimit > totSoftLimit) {
            float over = (Config::primaryEgtC(ed) - totSoftLimit) / (totHardLimit - totSoftLimit);
            target = constrain(target - over * 0.20f, 0.0f, target);
        }

        // Guard: if rampMs is 0 (instant) or dt is 0 (same-millisecond tick),
        // dividing would produce Inf or NaN.  Treat 0 ms ramp as instant (maxStep=1).
        float maxStep;
        if (target > _current) {
            maxStep = rampUpMs   > 0.0f ? dt / (rampUpMs   / 1000.0f) : 1.0f;
        } else {
            maxStep = rampDownMs > 0.0f ? dt / (rampDownMs / 1000.0f) : 1.0f;
        }
        _current = constrain(_current + constrain(target - _current, -maxStep, maxStep),
                             0.0f, 1.0f);

        ed.throttleDemand = _current;
    }

    void reset() override {
        _current = 0;
        _lastMs  = millis();
    }

    // Returns the slew-limited output before any external offset (e.g. AB fuel offset)
    // is added on top.  Used by checkABTrigger() to prevent the offset compounding
    // each tick when no physical throttle input is present to reset throttleDemand.
    float currentDemand() const { return _current; }

private:
    float         _current = 0;
    unsigned long _lastMs  = 0;
};
