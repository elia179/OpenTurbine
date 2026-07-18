#pragma once
#include "IController.h"
#include "../EngineData.h"
#include "../../system/Config.h"
#include "../../system/HardwareConfig.h"
#include "../../system/FeedbackRequirements.h"
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
//
//  RPM-limiter mode:
//    Simple (0, default) — pulls back on the CURRENT shaft RPM
//      (reactive). Byte-for-byte the original behaviour.
//    Advanced (1) — pulls back on RPM projected forward from the
//      filtered acceleration (ed.n1RpmAccel / n2RpmAccel), so fuel
//      eases off BEFORE an overshoot during a fast spool, and the
//      throttle-open ramp softens as the predicted RPM nears the
//      soft limit. EGT pullback stays reactive in both modes
//      (temperature is not predicted).
// ============================================================

class ThrottleSlew : public IController {
public:
    // Config parameters (ms to ramp 0→100%)
    float rampUpMs          = 600.0f;
    float rampDownMs        = 800.0f;

    // Safety pullback thresholds (from config: RPM limit and selected EGT limit)
    float rpmSoftLimit      = 95000.0f;  // start pulling back at 95% of limit
    float rpmHardLimit      = 100000.0f;
    float totSoftLimit      = 700.0f;
    float totHardLimit      = 750.0f;
    bool  n1PullbackEnabled = true;
    bool  n2PullbackEnabled = false;
    bool  egtPullbackEnabled = true;
    float n2SoftLimit       = 0.0f;
    float n2HardLimit       = 0.0f;
    float minPullbackThrottle = 0.08f;
    float pullbackStrength  = 1.0f;

    // ── Advanced (predictive) RPM-limiter mode ────────────────
    // Simple (0) leaves every value below unused → identical behaviour.
    int   rpmLimiterMode            = 0;       // 0 = simple (reactive), 1 = advanced (predictive)
    float pullbackLookaheadMs       = 1500.0f; // project RPM this far ahead from accel
    float pullbackNearLimitRampUpMs = 4000.0f; // slower throttle-open ramp near the limit (≈25 %/s)
    float pullbackApproachZoneRpm   = 0.0f;    // RPM band below soft limit where softening begins (assigned in Hardware.h; 0 = auto)

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
        float actualDt    = (now - _lastMs) / 1000.0f;
        float dt          = actualDt;
        _lastMs           = now;
        if (dt <= 0.0f) dt = 0.001f;
        else if (dt > 0.05f) dt = 0.05f;

        float target = constrain(ed.throttleDemand, 0.0f, 1.0f);
        // Dry Bench Mode intentionally permits actuator travel without fitted
        // feedback. In normal operation this interlock must still prevent a
        // fuel increase with missing speed or temperature feedback.
        if (!ed.benchMode &&
            ((FeedbackRequirements::n1ForProtectionOrControl() && !ed.n1Healthy) ||
             (FeedbackRequirements::n2ForProtectionOrControl() && !ed.n2Healthy) ||
             (FeedbackRequirements::egtForProtectionOrControl() && !Config::primaryEgtHealthy(ed)))) {
            if (target > _current) target = _current;
        }

        // Safety pullback: approach overspeed
        // Guard: rpmHardLimit must be strictly above rpmSoftLimit — equal limits
        // would cause division by zero and NaN throttle demand.
        auto applyPullback = [&](float value, float soft, float hard, float authority) {
            if (hard <= soft || value <= soft) return;
            float over = constrain((value - soft) / (hard - soft), 0.0f, 1.0f);
            float floor = constrain(minPullbackThrottle, 0.0f, target);
            float reduction = over * authority * pullbackStrength;
            target = constrain(target - reduction, floor, target);
        };

        // RPM fed to the pullback: current (Simple) or accel-predicted (Advanced),
        // and effRampUpMs softens the throttle-open rate near the limit (Advanced
        // only). In Simple mode n1val/n2val == current RPM and effRampUpMs ==
        // rampUpMs, so the pullback and slew below are the original behaviour.
        float n1val = ed.n1Rpm;
        float n2val = ed.n2Rpm;
        float effRampUpMs = rampUpMs;
        if (rpmLimiterMode == 1) {
            const float horizon = pullbackLookaheadMs * 0.001f;
            n1val = ed.n1Rpm + fmaxf(0.0f, ed.n1RpmAccel) * horizon;   // only anticipate a rising shaft
            n2val = ed.n2Rpm + fmaxf(0.0f, ed.n2RpmAccel) * horizon;
            if (ed.n1Healthy && pullbackApproachZoneRpm > 0.0f) {
                const float approachStart = rpmSoftLimit - pullbackApproachZoneRpm;
                if (n1val > approachStart) {
                    const float f = constrain((n1val - approachStart) / pullbackApproachZoneRpm, 0.0f, 1.0f);
                    effRampUpMs = rampUpMs + (pullbackNearLimitRampUpMs - rampUpMs) * f;  // larger ms = slower open
                }
            }
        }
        if (n1PullbackEnabled && ed.n1Healthy) applyPullback(n1val, rpmSoftLimit, rpmHardLimit, 0.30f);
        if (n2PullbackEnabled && ed.n2Healthy) applyPullback(n2val, n2SoftLimit, n2HardLimit, 0.40f);
        if (egtPullbackEnabled && Config::primaryEgtHealthy(ed)) {
            applyPullback(Config::primaryEgtC(ed), totSoftLimit, totHardLimit, 0.20f);
        }

        // Guard: if rampMs is 0 (instant) or dt is 0 (same-millisecond tick),
        // dividing would produce Inf or NaN.  Treat 0 ms ramp as instant (maxStep=1).
        float maxStep;
        if (target > _current) {
            maxStep = effRampUpMs > 0.0f ? dt / (effRampUpMs / 1000.0f) : 1.0f;
        } else {
            float closeDt = fmaxf(dt, actualDt);
            maxStep = rampDownMs > 0.0f ? closeDt / (rampDownMs / 1000.0f) : 1.0f;
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
