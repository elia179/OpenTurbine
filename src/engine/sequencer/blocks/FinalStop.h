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
            // Unhealthy N1 must not read as "stopped" — the shaft may still be
            // spinning. Wait out timeoutMs instead (same policy as RPMDrop).
            // With no N1 sensor fitted at all there is nothing to verify —
            // complete immediately instead of stalling every shutdown.
            bool stopped = HardwareConfig::hasN1Rpm
                         ? (ed.n1Healthy && (ed.n1Rpm <= rpmZeroThreshold))
                         : true;
            if (stopped || (now - _entryMs) > timeoutMs) {
                // Cut main oil pump
                ed.oilTargetBar    = 0;
                ed.oilPumpPct = 0;
                // Start scavenge phase if pump is fitted and duration > 0
                if (oilScavengeMs > 0 && HardwareConfig::hasOilScavengePump) {
                    ed.oilScavengeDemand = 1.0f; ed.oilScavengeOn = true;
                    _stoppedMs = now;
                    _phase     = 1;
                } else {
                    clearWaitReason();
                    return BlockResult::Complete;
                }
            } else {
                char _buf[80];
                if (!ed.n1Healthy)
                    snprintf(_buf, sizeof(_buf), "N1 sensor unhealthy (waiting %lu ms timeout)", timeoutMs);
                else
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
        ed.oilScavengeDemand = 0.0f; ed.oilScavengeOn = false;
    }

private:
    unsigned long _entryMs   = 0;
    unsigned long _stoppedMs = 0;
    int           _phase     = 0;
};
