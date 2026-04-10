#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// Spool up to running RPM target.
// Starter continues until target RPM, then starter disengages.
// Fault if target not reached within timeout.
class Spool : public IBlock {
public:
    float         rpmTarget          = 32000.0f;
    unsigned long timeoutMs          = 12000;
    float         throttleIdle       = 0.08f;  // idle throttle during spool
    bool          cutStarterOnExit   = true;   // zero starter demand when spool completes
    bool          cutStarterEnOnExit = true;   // de-assert starter enable relay when spool completes
    float         runningOilMin      = 2.8f;   // oil pressure minimum to enforce once spooling

    const char* name() override { return "Spool"; }

    void onEnter() override {
        _entryMs = millis();
        auto& ed = EngineData::instance();
        ed.throttleDemand     = throttleIdle;
        ed.flameMonitorActive = true;
        ed.clusterCode        = 8;  // ClCode::WaitingN1Rise (flame confirmed, spooling)
        // Raise oil minimum to running threshold now that ignition is confirmed —
        // moved here from SafetyHold so low-oil protection is active during spool-up.
        ed.oilMinBar          = runningOilMin;
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();

        if (ed.n1Healthy && ed.n1Rpm >= rpmTarget) {
            clearWaitReason();
            return BlockResult::Complete;
        }
        unsigned long elapsed = millis() - _entryMs;
        if (elapsed > timeoutMs) {
            clearWaitReason();
            return ed.benchMode ? BlockResult::Complete : BlockResult::Fault;
        }
        char _buf[80];
        if (ed.benchMode)
            snprintf(_buf, sizeof(_buf), "[BENCH] Spool sim — %lu ms remaining", timeoutMs - elapsed);
        else
            snprintf(_buf, sizeof(_buf), "N1: %d / %d RPM", (int)ed.n1Rpm, (int)rpmTarget);
        setWaitReason(_buf);
        return BlockResult::Running;
    }

    void onExit() override {
        auto& ed = EngineData::instance();
        if (cutStarterOnExit)   ed.starterDemand  = 0;
        if (cutStarterEnOnExit) ed.starterEnabled = false;
    }

private:
    unsigned long _entryMs = 0;
};
