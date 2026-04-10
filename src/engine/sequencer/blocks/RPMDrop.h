#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// Wait for N1 to drop below threshold before starting cooldown spin.
// Has a timeout — if RPM sensor faults we proceed anyway.
class RPMDrop : public IBlock {
public:
    float         rpmThreshold = 5000.0f;
    unsigned long timeoutMs    = 15000;

    const char* name() override { return "RPMDrop"; }

    void onEnter() override {
        _entryMs = millis();
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();
        // Proceed if RPM is below threshold OR sensor is unhealthy OR timeout
        if ((ed.n1Healthy && ed.n1Rpm < rpmThreshold) ||
            !ed.n1Healthy ||
            (millis() - _entryMs) > timeoutMs) {
            return BlockResult::Complete;
        }
        return BlockResult::Running;
    }

    void onExit() override {}

private:
    unsigned long _entryMs = 0;
};
