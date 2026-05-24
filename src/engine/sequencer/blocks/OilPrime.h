#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// Pre-start oil priming gate.
// WITH oil pressure sensor: arms pump at target bar, waits for oil >= oilArmMinBar,
//   aborts on timeout (engine never fired — safe).
// WITHOUT oil pressure sensor: runs pump at startupOilPct % for timeoutMs, then
//   completes normally (no pressure feedback available).
class OilPrime : public IBlock {
public:
    float         oilArmBar        = 2.5f;
    float         oilArmMinBar     = 1.5f;
    unsigned long timeoutMs        = 3000;
    float         startupOilDemand = 2.5f;  // bar pressure target (with sensor)
    float         startupOilPct    = 80.0f; // pump duty % (no sensor)
    bool          useScavengePump  = false; // also activate scavenge pump during prime

    const char* name() override { return "OilPrime"; }

    void onEnter() override {
        _entryMs = millis();
        auto& ed = EngineData::instance();
        ed.oilMinBar = 0;  // not yet checking min during prime
        if (HardwareConfig::hasOilPress) {
            ed.oilTargetBar = startupOilDemand;   // P-controller will regulate to this bar target
        } else {
            ed.oilPumpPct = startupOilPct;   // fixed % — no sensor, run for timeout
        }
        if (useScavengePump) ed.oilScavengeOn = true;
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();

        unsigned long elapsed = millis() - _entryMs;

        if (!HardwareConfig::hasOilPress || ed.benchMode) {
            // No pressure sensor, OR bench mode — run pump for configured time then proceed
            if (elapsed >= timeoutMs) {
                clearWaitReason();
                return BlockResult::Complete;
            }
            char buf[80];
            snprintf(buf, sizeof(buf), "%sOil pump %.0f%% — %lu ms remaining",
                     ed.benchMode ? "[BENCH] " : "",
                     HardwareConfig::hasOilPress ? (float)100 : startupOilPct,
                     timeoutMs - elapsed);
            setWaitReason(buf);
            return BlockResult::Running;
        }

        // Pressure sensor present and not bench mode — wait for pressure to come up
        if (ed.oilHealthy && ed.oilPressure >= oilArmMinBar) {
            clearWaitReason();
            return BlockResult::Complete;
        }
        if (elapsed > timeoutMs) {
            clearWaitReason();
            return BlockResult::Abort;  // oil never came up — safe to abort
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "Oil pressure: %.2f / %.2f bar", ed.oilPressure, oilArmMinBar);
        setWaitReason(buf);
        return BlockResult::Running;
    }

    void onExit() override {
        // Arm oil safety check — sequencer sets the real threshold in StarterSpin
        EngineData::instance().oilMinBar = oilArmMinBar;
    }

private:
    unsigned long _entryMs = 0;
};
