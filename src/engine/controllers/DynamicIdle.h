#pragma once
#include "IController.h"
#include "../EngineData.h"
#include "../../system/Config.h"
#include "../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  DynamicIdle — closed-loop idle RPM hold
//
//  Reads N1 (or N2 if idleUseN2 set in Config) from EngineData.
//  Adjusts throttleDemand floor to maintain target RPM.
//
//  Simple mode (idleMode 0, default): PI controller.
//    Proportional path: asymmetric ramp (up slowly, down faster).
//    Deadband prevents micro-corrections.
//    Integral path: accumulates steady-state error when I-gain > 0.
//      Eliminates offset caused by load changes (e.g. oil pump turn-on).
//      Windup limited to ±idleIMax (fraction of full throttle range).
//      Integral resets to zero on begin()/reset().
//
//  Advanced mode (idleMode 1): decel-catch + learned-hold + predictive
//    trim. Replaces the integrator with a learned idle-hold (running both
//    double-integrates and hunts). On a fast chop from high RPM it drops
//    just below the learned hold (decel-catch) so the engine settles to
//    idle without hanging high or dipping toward flameout; near idle it
//    trims against RPM projected from ed.n1RpmAccel/n2RpmAccel and re-learns
//    the hold once genuinely settled. The learned hold persists across
//    disengage/re-engage within a run (only begin()/reset() clear it) and is
//    intentionally NOT persisted to flash — idle hold is warm-state dependent.
//
//  Disengages above rpmLimit (operator throttle takes over).
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
    float minMultiplier = 0.75f;     // floor = fuel-pump min-spin * multiplier
    float maxMultiplier = 1.50f;     // ceiling = configured idle max throttle * multiplier

    // ── Advanced (decel-catch) mode — Simple (0) = current PI behaviour ──
    int   idleMode              = 0;        // 0 = simple (PI), 1 = advanced (decel-catch + learned hold)
    float idleDecelEnterRpm     = 1000.0f;  // enter decel-catch when rpm > target + this
    float idleDecelDropPct      = 2.0f;     // drop below the learned hold during decel-catch (%)
    float idleLookaheadMs       = 2500.0f;  // project rpm this far ahead (signed) for the trim
    float idleSettleBandRpm     = 1500.0f;  // no trim / begin learning inside this band
    float idleFullResponseRpm   = 12000.0f; // error at which the trim reaches full rate
    float idleTrimUpPctPerSec   = 4.0f;     // max trim-up rate (%/s)
    float idleTrimDownPctPerSec = 2.0f;     // max trim-down rate (%/s)
    float idleLearnRate         = 0.02f;    // EMA weight when learning the hold
    float idleLearnAccelMax     = 1200.0f;  // only learn the hold while |accel| below this (RPM/s)

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

        const bool hasEffectiveN2 = HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm;
        bool  useN2   = Config::idleUseN2 || (!HardwareConfig::hasN1Rpm && hasEffectiveN2);
        if (useN2 && !hasEffectiveN2) {
            reset();
            return;
        }
        if (!useN2 && !HardwareConfig::hasN1Rpm) {
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

        // Disengage at high RPM or if sensor not trustworthy.
        // _learnedHold is intentionally retained across disengage/re-engage
        // (only begin()/reset() clear it); _wasEngaged re-arms the advanced
        // state machine on the next engagement. _integrator=0 is the Simple path.
        if (!healthy || rpm > rpmLimit) {
            _idleFloor  = 0;
            _integrator = 0;
            _wasEngaged = false;
            return;
        }

        // Floor / ceiling bounds — shared by both modes (computation unchanged).
        // This multiplier belongs to the calibrated fuel-pump minimum spin.
        // Deriving it from RPM ratios can impose a hidden high fuel
        // minimum (for example 54% instead of an intended 7.2%).
        float minFloor = constrain((Config::fuelPumpMinPct / 100.0f) * minMultiplier,
                                   0.0f, 1.0f);
        // Ceiling near the calibrated idle max: a healthy-but-underreading N1
        // (e.g. wrong pulses-per-rev) keeps error positive forever and would
        // otherwise wind the floor to 100% throttle that the operator cannot
        // override. The multiplier leaves headroom for load steps (oil pump,
        // cold oil) without giving the loop full-throttle authority.
        float maxFloor = constrain((Config::throttleIdleMaxPct / 100.0f) * maxMultiplier,
                                   minFloor, 1.0f);

        // ── Advanced mode: decel-catch + learned hold + predictive trim ──
        if (idleMode == 1) {
            if (!_wasEngaged) {                 // fresh engagement: choose a state
                _wasEngaged = true;
                _state = (rpm > targetRpm + idleDecelEnterRpm && _learnedHoldValid)
                         ? DiState::DecelCatch : DiState::Trim;
            }
            float accel   = useN2 ? ed.n2RpmAccel : ed.n1RpmAccel;
            float predRpm = rpm + accel * (idleLookaheadMs * 0.001f);   // signed prediction

            if (_state == DiState::DecelCatch) {
                _idleFloor = constrain(_learnedHold - idleDecelDropPct / 100.0f, minFloor, maxFloor);
                if (rpm <= targetRpm + idleDecelEnterRpm) _state = DiState::Trim;
            } else { // Trim
                float err = targetRpm - predRpm, ae = fabsf(err);
                if (ae > idleSettleBandRpm) {
                    float resp = constrain((ae - idleSettleBandRpm) / fmaxf(1.0f, idleFullResponseRpm), 0.0f, 1.0f);
                    float ratePctS = (err > 0 ? idleTrimUpPctPerSec : idleTrimDownPctPerSec);
                    float stepFrac = (ratePctS / 100.0f) * resp * dt;   // %/s → fraction this tick
                    _idleFloor = constrain(_idleFloor + (err > 0 ? +stepFrac : -stepFrac), minFloor, maxFloor);
                }
                // Learn the hold only when genuinely settled and not accelerating.
                if (fabsf(rpm - targetRpm) <= idleSettleBandRpm &&
                    fabsf(accel) <= idleLearnAccelMax) {
                    float nominalAlpha = constrain(idleLearnRate, 0.0f, 1.0f);
                    float alpha = nominalAlpha >= 1.0f ? 1.0f
                                : 1.0f - powf(1.0f - nominalAlpha, dt * 400.0f);
                    _learnedHold += (_idleFloor - _learnedHold) * alpha;
                    _learnedHoldValid = true;
                }
            }
            float demand = constrain(_idleFloor, minFloor, maxFloor);
            if (ed.throttleDemand < demand) ed.throttleDemand = demand;
            return;
        }

        // ── Simple mode (idleMode 0): PI controller — behaviour unchanged ──
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
        _idleFloor = constrain(_idleFloor + rampStep, minFloor, maxFloor);

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
        float demand = constrain(_idleFloor + _integrator, minFloor, maxFloor);

        // Apply as minimum floor on throttleDemand
        if (ed.throttleDemand < demand) {
            ed.throttleDemand = demand;
        }
    }

    void reset() override {
        _idleFloor        = 0;
        _integrator       = 0;
        _learnedHold      = 0.0f;
        _learnedHoldValid = false;
        _wasEngaged       = false;
        _state            = DiState::Trim;
        _lastMs           = millis();
    }

private:
    enum class DiState : uint8_t { Trim, DecelCatch };
    DiState       _state            = DiState::Trim;
    float         _idleFloor        = 0;
    float         _integrator       = 0;
    float         _learnedHold      = 0.0f;
    bool          _learnedHoldValid = false;
    bool          _wasEngaged       = false;
    unsigned long _lastMs           = 0;
};
