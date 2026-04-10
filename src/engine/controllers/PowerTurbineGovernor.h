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
//    - P-controller with deadband (no integral to avoid windup
//      when load steps occur — the throttle slew already limits
//      rate-of-change).
//    - Disengages automatically when N2 is not healthy.
//    - Interacts with ThrottleSlew: governor sets throttleDemand
//      directly; ThrottleSlew then rate-limits it. Set rampUpMs
//      conservatively so the governor does not fight slew.
//
//  Prop pitch actuator management:
//    If hasPropPitch is enabled, the governor prefers to adjust
//    propPitchDemand first (torque / load change) and only falls
//    back to throttleDemand when pitch reaches limits.  This
//    matches conventional turboprop governing where the propeller
//    governor is the primary speed control authority.
//
//  Config params (set from Config before begin()):
//    targetRpm   — desired N2 speed (RPM)
//    bandRpm     — deadband ±RPM (no correction inside this band)
//    kp          — proportional gain: throttle fraction per RPM error
//                  e.g. 0.001 → 1 % throttle per 1 RPM error
//    pitchKp     — pitch fraction per RPM error (turboprop mode)
// ============================================================

class PowerTurbineGovernor : public IController {
public:
    float targetRpm = 0.0f;      // 0 = disabled
    float bandRpm   = 500.0f;
    float kp        = 0.001f;    // throttle %/RPM (fraction/RPM)
    float pitchKp   = 0.0005f;   // pitch demand fraction/RPM error
    bool  usePropPitch = false;  // true = turboprop pitch-primary mode

    void begin() override {
        reset();
    }

    void tick() override {
        auto& ed = EngineData::instance();
        if (targetRpm <= 0.0f) {
            // Governor disabled — release pitch to fine (unloaded) so manual
            // throttle takes full authority without fighting a stale demand.
            if (_wasActive) {
                ed.propPitchDemand = 0.0f;
                _wasActive = false;
            }
            return;
        }
        _wasActive = true;
        if (!ed.n2Healthy) return;

        float error = targetRpm - ed.n2Rpm;
        if (fabsf(error) <= bandRpm) return;   // inside deadband

        if (usePropPitch) {
            // Turboprop: pitch primary, throttle secondary
            float pitchAdj = pitchKp * error;
            float newPitch = constrain(ed.propPitchDemand + pitchAdj, 0.0f, 1.0f);
            if ((error > 0 && newPitch < 1.0f) || (error < 0 && newPitch > 0.0f)) {
                ed.propPitchDemand = newPitch;
                return;  // pitch has authority; don't touch throttle
            }
        }

        // Throttle-primary (or pitch authority exhausted)
        float adj = kp * error;
        ed.throttleDemand = constrain(ed.throttleDemand + adj, 0.0f, 1.0f);
    }

    void reset() override {
        _wasActive = false;
    }

private:
    bool _wasActive = false;
};
