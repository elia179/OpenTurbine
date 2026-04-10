#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// Fire igniter for a fixed pre-ignition dwell period.
// Always completes (timer only). Igniter stays on through FuelOpen.
class PreIgnSpark : public IBlock {
public:
    unsigned long sparkMs = 1500;

    const char* name() override { return "PreIgnSpark"; }

    void onEnter() override {
        _entryMs = millis();
        auto& ed = EngineData::instance();
        ed.igniterOn    = true;
        ed.clusterCode  = 5;  // ClCode::Igniting
    }

    BlockResult tick() override {
        if ((millis() - _entryMs) >= sparkMs) return BlockResult::Complete;
        return BlockResult::Running;
    }

    void onExit() override {}  // igniter stays on — turned off after FlameConfirm

private:
    unsigned long _entryMs = 0;
};
