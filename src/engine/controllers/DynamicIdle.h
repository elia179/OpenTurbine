#pragma once
#include "IController.h"
#include "../EngineData.h"
#include "../../system/Config.h"
#include "../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  DynamicIdle — closed-loop idle RPM hold (PI controller)
//
//  Reads N1 (or N2 if idleUseN2 set in Config) from EngineData.
//  Adjusts throttleDemand floor to maintain target RPM.
//
//  Proportional path: asymmetric ramp (up slowly, down faster).
//  Deadband prevents micro-corrections.
//  Integral path: accumulates steady-state error when I-gain > 0.
//    Eliminates offset caused by load changes (e.g. oil pump turn-on).
//    Windup limited to ±idleIMax (fraction of full throttle range).
//    Integral resets to zero on begin()/reset().
//  Disengages above rpmLimit (pilot throttle takes over).
//  Only active when dynamicIdleEnabled = true in EngineData.
// ============================================================

class DynamicIdle : public IController {
public:
    // Config parameters
    float targetRpm     = 44000.0f;
    float rampUpMs      = 10000.0f;  // ms to ramp across full range
    float rampDownMs    = 20000.0f;
    float deadbandRpm   = 300.0f;
    float rpmLimit      = 60000.0f;  // disengage above this
    float minMultiplier = 0.75f;     // floor = configured idle throttle * multiplier

    void begin() override {
        reset();
        _lastMs = millis();
    }

    void tick() override {
        auto& ed = EngineData::instance();

        if (!ed.dynamicIdleEnabled) return;
        // Misconfigured limits would cause division by zero — bail out safely.
        if (targetRpm <= 0.0f || rpmLimit <= 0.0f) {
            reset();
            return;
        }

        bool  useN2   = Config::idleUseN2;
        if (useN2 && !HardwareConfig::hasN2Rpm) {
            reset();
            return;
        }
        float rpm     = useN2 ? ed.n2Rpm    : ed.n1Rpm;
        bool healthy  = useN2 ? ed.n2Healthy : ed.n1Healthy;

        // Always advance _lastMs so dt is never stale when we re-engage.
        // Without this, a long disengage period (high RPM, unhealthy sensor)
        // causes the first re-engage tick to compute a huge dt → maxStep → a
        // single-tick floor jump of up to 89 % instead of a smooth ramp.
        unsigned long now = millis();
        float dt          = (now - _lastMs) / 1000.0f;
        _lastMs           = now;
        if (dt <= 0.0f) dt = 0.001f;
        else if (dt > 0.05f) dt = 0.05f;

        // Disengage at high RPM or if sensor not trustworthy
        if (!healthy || rpm > rpmLimit) {
            _idleFloor  = 0;
            _integrator = 0;
            return;
        }

        float error = targetRpm - rpm;

        // ── Proportional / ramp path ───────────────────────────
        // _idleFloor converges toward the needed throttle level via
        // rate-limited steps. This is the primary correction path.
        float rampStep = 0.0f;
        if (fabsf(error) >= deadbandRpm) {
            float rampMs = (error > 0) ? rampUpMs : rampDownMs;
            float maxStep = rampMs > 0.0f ? dt / (rampMs / 1000.0f) : 1.0f;
            // Scale error as fraction of targetRpm so the proportional zone
            // is centred on the idle point rather than the disengage ceiling.
            rampStep = constrain(error / targetRpm, -maxStep, maxStep);
        }
        // This multiplier belongs to the calibrated running idle throttle
        // floor. Deriving it from RPM ratios can impose a hidden high fuel
        // minimum (for example 54% instead of an intended 7.2%).
        float minFloor = constrain((Config::throttleIdleMinPct / 100.0f) * minMultiplier,
                                   0.0f, 1.0f);
        _idleFloor = constrain(_idleFloor + rampStep, minFloor, 1.0f);

        // ── Integral path (optional — off when idleIGain = 0) ──
        // Accumulates persistent steady-state error and adds an offset to
        // the floor. Eliminates droop from load steps (oil pump, AB pump).
        // The integral is separate from _idleFloor so the ramp path is not
        // contaminated; windup is capped at ±idleIMax.
        float iGain = Config::idleIGain;
        float iMax  = Config::idleIMax;
        if (iGain > 0.0f) {
            _integrator += (error / targetRpm) * iGain * dt;
            _integrator  = constrain(_integrator, -iMax, iMax);
        } else {
            _integrator = 0;
        }

        // Final demand = ramp floor + integral correction
        float demand = constrain(_idleFloor + _integrator, minFloor, 1.0f);

        // Apply as minimum floor on throttleDemand
        if (ed.throttleDemand < demand) {
            ed.throttleDemand = demand;
        }
    }

    void reset() override {
        _idleFloor  = 0;
        _integrator = 0;
        _lastMs     = millis();
    }

private:
    float         _idleFloor  = 0;
    float         _integrator = 0;
    unsigned long _lastMs     = 0;
};
