#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// Final shutdown: wait for N1=0, cut main oil, run scavenge pump (if fitted), enter STANDBY.
class FinalStop : public IBlock {
public:
    float         rpmZeroThreshold = 100.0f;
    unsigned long timeoutMs        = 10000;
    unsigned long oilScavengeMs    = 0;      // scavenge pump extra runtime after main oil cut (0 = disabled)

    const char* name() override { return "FinalStop"; }

    void onEnter() override {
        _entryMs    = millis();
        _stoppedMs  = 0;
        _phase      = 0;
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();
        unsigned long now = millis();

        if (_phase == 0) {
            bool stopped = (!ed.n1Healthy) || (ed.n1Rpm < rpmZeroThreshold);
            if (stopped || (now - _entryMs) > timeoutMs) {
                // Cut main oil pump
                ed.oilTargetBar    = 0;
                ed.oilPumpPct = 0;
                // Start scavenge phase if pump is fitted and duration > 0
                if (oilScavengeMs > 0 && HardwareConfig::hasOilScavengePump) {
                    ed.oilScavengeOn = true;
                    _stoppedMs = now;
                    _phase     = 1;
                } else {
                    clearWaitReason();
                    return BlockResult::Complete;
                }
            } else {
                char _buf[80];
                snprintf(_buf, sizeof(_buf), "N1: %d RPM (waiting for zero)", (int)ed.n1Rpm);
                setWaitReason(_buf);
            }
        }
        if (_phase == 1) {
            if ((now - _stoppedMs) >= oilScavengeMs) {
                clearWaitReason();
                return BlockResult::Complete;
            }
            char _buf[80];
            snprintf(_buf, sizeof(_buf), "Scavenge: %lu ms remaining", oilScavengeMs - (now - _stoppedMs));
            setWaitReason(_buf);
        }
        return BlockResult::Running;
    }

    void onExit() override {
        auto& ed = EngineData::instance();
        ed.oilTargetBar     = 0;
        ed.oilPumpPct  = 0;
        ed.oilScavengeOn = false;
    }

private:
    unsigned long _entryMs   = 0;
    unsigned long _stoppedMs = 0;
    int           _phase     = 0;
};
