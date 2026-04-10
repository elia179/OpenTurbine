#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// Wait for TOT to rise above a target temperature.
// Alternative to FlameConfirm for engines without a flame sensor.
// Requires requiredCount consecutive readings above tempTarget to avoid
// triggering on a single sensor glitch.
// Abort if timeout is reached without TOT meeting the threshold.
class TempConfirm : public IBlock {
public:
    float         tempTarget    = 200.0f;   // °C
    unsigned long timeoutMs     = 10000;
    int           requiredCount = 3;        // consecutive readings above target needed

    const char* name() override { return "TempConfirm"; }

    void onEnter() override {
        _entryMs = millis();
        _count   = 0;
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();
        if ((millis() - _entryMs) > timeoutMs) return BlockResult::Abort;
        if (ed.totHealthy && ed.tot >= tempTarget) {
            if (++_count >= requiredCount) return BlockResult::Complete;
        } else {
            _count = 0;  // reset on any reading below threshold
        }
        return BlockResult::Running;
    }

    void onExit() override {}

private:
    unsigned long _entryMs = 0;
    int           _count   = 0;
};
