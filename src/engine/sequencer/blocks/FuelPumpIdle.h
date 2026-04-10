#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// ============================================================
//  FuelPumpIdle — set throttle / fuel ESC from idle input channel
//
//  ACTION block: reads idleInputRaw (0–4095), maps it linearly
//  through [minPct, maxPct], and writes the result to
//  ed.throttleDemand.  Completes in one tick.
//
//  Typical idle range: 8–18 % of full throttle travel.
//  minPct / maxPct are set from Config::fuelPumpIdleMinPct /
//  fuelPumpIdleMaxPct via Hardware::applyConfig().
// ============================================================
class FuelPumpIdle : public IBlock {
public:
    float minPct = 8.0f;    // throttle % when idle input is at minimum
    float maxPct = 18.0f;   // throttle % when idle input is at maximum

    const char* name() override { return "FuelPumpIdle"; }

    void onEnter() override {
        auto& ed = EngineData::instance();
        float norm = constrain(ed.idleInputRaw / 4095.0f, 0.0f, 1.0f);
        float pct  = minPct + norm * (maxPct - minPct);
        ed.throttleDemand = constrain(pct / 100.0f, 0.0f, 1.0f);
    }

    BlockResult tick() override {
        return BlockResult::Complete;
    }

    void onExit() override {}
};
