#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/Config.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  ModifiedIdle — set throttle demand from idle input × multiplier
//
//  ACTION block: reads idleInputRaw (0–4095), maps it through the
//  configured idle range [fuelPumpMinPct, throttleIdleMaxPct],
//  then multiplies by `multiplier` and writes to ed.throttleDemand.
//  Completes in one tick.
//
//  multiplier is a per-block runtime param set from Config via
//  Hardware::applyConfig(). The low end is the calibrated fuel-pump
//  minimum-spin percentage; the high end comes from throttleIdleMaxPct.
//
//  Use case: hold throttle at a scaled idle position — e.g., 1.5× idle
//  for a warm-up step without engaging full DynamicIdle control.
// ============================================================
class ModifiedIdle : public IBlock {
public:
    float multiplier = 1.0f;

    const char* name() override { return "ModifiedIdle"; }

    void onEnter() override {
        auto& ed = EngineData::instance();
        float norm;
        if (HardwareConfig::idleInputRcPwm) {
            norm = ed.rcIdleValid ? ed.rcIdleNorm : 0.0f;
        } else {
            int range = Config::idleMaxRaw - Config::idleMinRaw;
            norm = range == 0 ? 0.0f :
                constrain((ed.idleInputRaw - Config::idleMinRaw) / (float)range, 0.0f, 1.0f);
        }
        float minPct = Config::fuelPumpMinPct;
        float maxPct = constrain(Config::throttleIdleMaxPct, minPct, 100.0f);
        float pct    = minPct + norm * (maxPct - minPct);
        ed.throttleDemand = constrain((pct / 100.0f) * multiplier, 0.0f, 1.0f);
    }

    BlockResult tick() override {
        return BlockResult::Complete;
    }

    void onExit() override {}
};
