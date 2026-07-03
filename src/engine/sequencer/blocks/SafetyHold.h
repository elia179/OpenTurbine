#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// Post-spool safety hold: verify stable RPM and oil pressure
// before declaring RUNNING. Fault if conditions not met.
class SafetyHold : public IBlock {
public:
    unsigned long holdMs              = 1000;
    float         finalCheckRpm      = 31000.0f;
    float         runningOilMin      = 2.8f;

    // Optional exit actions — all off by default (no change from previous behaviour)
    bool          turnOffStarterOnExit   = false;
    bool          turnOffStarterEnOnExit = false;
    bool          turnOffIgniterOnExit   = false;

    const char* name() override { return "SafetyHold"; }

    void onEnter() override {
        _entryMs = millis();
        // oilMinBar is set to runningOilMin in Spool::onEnter (after ignition is
        // confirmed). Avoid overwriting it here — Spool already raised the threshold.
        // Left in place as a safety backstop in case Spool is skipped in sequence.
        if (EngineData::instance().oilMinBar < runningOilMin) {
            EngineData::instance().oilMinBar = runningOilMin;
        }
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();

        unsigned long elapsed = millis() - _entryMs;
        if (elapsed < holdMs) {
            char _buf[80];
            snprintf(_buf, sizeof(_buf), "Safety hold: %lu ms remaining", (holdMs - elapsed));
            setWaitReason(_buf);
            return BlockResult::Running;
        }
        clearWaitReason();

        // Final sanity check before handing off to RUNNING
        // (skipped in bench mode — no real sensors)
        if (!ed.benchMode) {
            if (!HardwareConfig::hasN1Rpm && !HardwareConfig::hasOilPress) return BlockResult::Fault;
            if (HardwareConfig::hasN1Rpm &&
                (!ed.n1Healthy || ed.n1Rpm < finalCheckRpm)) return BlockResult::Fault;
            if (HardwareConfig::hasOilPress &&
                (!ed.oilHealthy || ed.oilPressure < runningOilMin)) return BlockResult::Fault;
        }

        return BlockResult::Complete;
    }

    void onExit() override {
        auto& ed = EngineData::instance();
        if (turnOffStarterOnExit)   ed.starterDemand  = 0;
        if (turnOffStarterEnOnExit) ed.starterEnabled = false;
        if (turnOffIgniterOnExit) {
            ed.igniterOn = false;
            ed.igniter2On = false;
            ed.glowPlugDemand = 0.0f;
        }
    }

private:
    unsigned long _entryMs = 0;
};
