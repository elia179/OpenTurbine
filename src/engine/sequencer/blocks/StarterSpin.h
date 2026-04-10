#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// Spin engine to pre-ignition RPM using starter motor.
// Ramps starter ESC from 0 to starterDemand at rampPctPerSec.
// Fault if target RPM not reached within timeout.
class StarterSpin : public IBlock {
public:
    float         starterDemand   = 0.60f;
    float         targetRpm       = 5000.0f;
    unsigned long timeoutMs       = 8000;
    float         oilStartupMinBar = 1.5f;
    float         rampPctPerSec   = 10.0f;  // % per second ramp rate (0 = instant) — matches Config::starterRampPctPerSec default

    const char* name() override { return "StarterSpin"; }

    void onEnter() override {
        _entryMs      = millis();
        _lastRampMs   = millis();
        _currentDemand = 0.0f;
        auto& ed = EngineData::instance();
        ed.starterEnabled = true;
        ed.starterDemand  = 0.0f;   // ramp starts from 0
        ed.oilMinBar      = oilStartupMinBar;
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();

        // Slew rate: ramp demand toward target
        if (rampPctPerSec > 0.0f && _currentDemand < starterDemand) {
            unsigned long now = millis();
            float dtSec       = (now - _lastRampMs) * 0.001f;
            _lastRampMs       = now;
            float step        = rampPctPerSec * 0.01f * dtSec;  // pct/s → fraction/s * dt
            _currentDemand    = fminf(_currentDemand + step, starterDemand);
            ed.starterDemand  = _currentDemand;
        } else {
            ed.starterDemand = starterDemand;
        }

        if (ed.n1Healthy && ed.n1Rpm >= targetRpm) {
            clearWaitReason();
            return BlockResult::Complete;
        }
        unsigned long elapsed = millis() - _entryMs;
        if (elapsed > timeoutMs) {
            clearWaitReason();
            // Bench mode: proceed anyway (no real engine); normal: fault → shutdown
            return ed.benchMode ? BlockResult::Complete : BlockResult::Fault;
        }
        char _buf[80];
        if (ed.benchMode)
            snprintf(_buf, sizeof(_buf), "[BENCH] Starter sim — %lu ms remaining", timeoutMs - elapsed);
        else
            snprintf(_buf, sizeof(_buf), "N1: %d / %d RPM", (int)ed.n1Rpm, (int)targetRpm);
        setWaitReason(_buf);
        return BlockResult::Running;
    }

    void onExit() override {}

private:
    unsigned long _entryMs      = 0;
    unsigned long _lastRampMs   = 0;
    float         _currentDemand = 0.0f;
};
