#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// ============================================================
//  ABStabilize — hold after AB lights, monitor TOT
//
//  Waits stabilizeMs to confirm a stable AB burn.
//  If stabilizeMaxTot > 0 and TOT exceeds it → Fault.
//  On Complete: sets abMode = Running.
// ============================================================

class ABStabilize : public IBlock {
public:
    int   stabilizeMs     = 1000;
    float stabilizeMaxTot = 0.0f;    // 0 = disabled

    const char* name() override { return "ABStabilize"; }

    void onEnter() override {
        _startMs = millis();
        _completed = false;
        Serial.printf("[AB] Stabilize: holding %d ms\n", stabilizeMs);
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();

        // TOT guard
        if (stabilizeMaxTot > 0 && !ed.totHealthy) {
            Serial.println("[AB] Stabilize fault: TOT sensor unavailable");
            return BlockResult::Fault;
        }
        if (stabilizeMaxTot > 0 && ed.totHealthy && ed.tot > stabilizeMaxTot) {
            Serial.printf("[AB] Stabilize: TOT %.1f > limit %.1f — FAULT\n",
                          (double)ed.tot, (double)stabilizeMaxTot);
            return BlockResult::Fault;
        }

        if ((millis() - _startMs) >= (unsigned long)stabilizeMs) {
            _completed = true;
            return BlockResult::Complete;
        }
        return BlockResult::Running;
    }

    void onExit() override {
        if (!_completed) return;
        // Signal the AB state machine that AB is now running
        EngineData::instance().abMode = ABMode::Running;
        Serial.println("[AB] Stabilize: complete — AB RUNNING");
    }

private:
    unsigned long _startMs = 0;
    bool          _completed = false;
};
