#pragma once
#include "IController.h"
#include "../EngineData.h"
#include <Arduino.h>

// ============================================================
//  PowerTurbineGovernor — N2 speed-hold for turboshaft / APU
//
//  In turboshaft and APU applications the power turbine (N2)
//  must run at a constant speed regardless of load (helicopter
//  rotor, generator, gearbox).  This controller adjusts the
//  main throttleDemand to hold N2 at targetRpm.
//
//  Architecture:
//    - P-controller, dt-scaled so that gain is independent of
//      main-loop call rate (previous version applied kp*error
//      per tick, making response dependent on CPU load / WiFi
//      processing delays).
//    - Deadband (bandRpm): no correction while inside.
//    - Disengages automatically when N2 is not healthy.
//    - Interacts with ThrottleSlew: governor sets throttleDemand
//      directly; ThrottleSlew then rate-limits it.  Set rampUpMs
//      conservatively so the governor does not fight slew.
//
//  Prop pitch actuator management (turboprop mode):
//    If usePropPitch is true the governor prefers to adjust
//    propPitchDemand first (torque / load change) and only falls
//    back to throttleDemand when pitch reaches its limits.  This
//    matches conventional turboprop governing where the propeller
//    governor is the primary speed-control authority.
//
//    Pitch is slew-rate limited (pitchRampSec: seconds for a
//    full 0→100 % stroke).  The P-gain is also dt-scaled, so
//    the effective rate of change is the lesser of the gain-
//    driven rate and the hard slew cap.
//
//    Internal pitch state (_pitchCurrent) is tracked separately
//    from ed.propPitchDemand so successive adjustments never
//    accumulate floating-point rounding error.
//
//  Config params (set from Config / Hardware::applyConfig()):
//    targetRpm      — desired N2 speed (RPM); 0 = governor disabled
//    bandRpm        — deadband ±RPM (no correction inside this band)
//    kp             — throttle gain: fraction/RPM·s  (e.g. 0.001)
//    pitchKp        — pitch gain:   fraction/RPM·s  (e.g. 0.0005)
//    pitchRampSec   — max pitch travel: full stroke in this many
//                     seconds (0 = unlimited; default 10 s)
// ============================================================

class PowerTurbineGovernor : public IController {
public:
    float targetRpm    = 0.0f;      // 0 = disabled
    float bandRpm      = 500.0f;
    float kp           = 0.001f;    // throttle fraction per RPM·s
    float pitchKp      = 0.0005f;   // pitch fraction per RPM·s
    float pitchRampSec = 10.0f;     // hard slew limit for pitch (0=off)
    bool  usePropPitch = false;     // true = turboprop pitch-primary mode

    void begin() override {
        auto& ed = EngineData::instance();
        // If the governor was previously active and holding a non-zero pitch,
        // release pitch to fine (0) before re-engaging.  Without this, a
        // relight or RUNNING re-entry leaves pitch stranded at its last
        // position.  If targetRpm > 0 the governor will immediately start
        // adjusting pitch again on the first tick.
        if (usePropPitch && _wasActive) {
            ed.propPitchDemand = 0.0f;
            _pitchCurrent      = 0.0f;
        }
        _wasActive = false;
        _lastMs    = millis();
    }

    void tick() override {
        auto& ed = EngineData::instance();
        unsigned long now = millis();
        float dt = (now - _lastMs) / 1000.0f;
        _lastMs  = now;
        // Guard: clamp dt so a stale timer or first-call edge case doesn't
        // produce an absurdly large correction step.
        if (dt <= 0.0f || dt > 0.5f) dt = 0.01f;

        if (targetRpm <= 0.0f) {
            // Governor disabled — release pitch to fine so manual throttle
            // has full authority without fighting a stale pitch demand.
            if (_wasActive) {
                if (usePropPitch) {
                    ed.propPitchDemand = 0.0f;
                    _pitchCurrent      = 0.0f;
                }
                _wasActive = false;
            }
            return;
        }
        _wasActive = true;
        if (!ed.n2Healthy) return;

        float error = targetRpm - ed.n2Rpm;
        if (fabsf(error) <= bandRpm) return;   // inside deadband — no action

        if (usePropPitch) {
            // Turboprop: pitch primary, throttle secondary.
            // dt-scaled gain + hard slew cap so large errors can't step pitch
            // instantaneously (which would cause gearbox torque transients and
            // immediate N2 excursions that the governor then has to chase).
            float pitchAdj    = pitchKp * error * dt;
            float maxStep     = pitchRampSec > 0.0f ? dt / pitchRampSec : 1.0f;
            pitchAdj          = constrain(pitchAdj, -maxStep, maxStep);
            float newPitch    = constrain(_pitchCurrent + pitchAdj, 0.0f, 1.0f);

            // Apply only if pitch has authority in the required direction
            if ((error > 0 && newPitch < 1.0f) || (error < 0 && newPitch > 0.0f)) {
                _pitchCurrent      = newPitch;
                ed.propPitchDemand = newPitch;
                return;  // pitch authority: skip throttle
            }
            // Pitch saturated — fall through to throttle
        }

        // Throttle-primary (or pitch authority exhausted).
        // dt-scaled so effective gain is loop-rate-independent.
        float adj = kp * error * dt;
        ed.throttleDemand = constrain(ed.throttleDemand + adj, 0.0f, 1.0f);
    }

    void reset() override {
        _wasActive    = false;
        _lastMs       = millis();
        // Do not zero _pitchCurrent here: reset() is used mid-run by some callers.
        // begin() is the authoritative "start fresh" path that handles pitch release.
    }

private:
    bool          _wasActive    = false;
    unsigned long _lastMs       = 0;
    float         _pitchCurrent = 0.0f;  // governor's own pitch position state
};
