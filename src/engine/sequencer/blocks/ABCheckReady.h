#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/Config.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  ABCheckReady — gate block before AB ignition sequence
//
//  Checks that N1, TOT and throttle position are within the
//  configured windows for a safe AB ignition attempt.
//
//  Completes immediately if all conditions are met.
//  Aborts if any hard limit is exceeded.
//
//  All thresholds are copied from Config at applyConfig() time.
// ============================================================

class ABCheckReady : public IBlock {
public:
    float minN1          = 30000.0f;  // N1 must be >= this
    float maxN1          = 0.0f;      // N1 must be <= this (0 = disabled)
    float maxTotForLight = 0.0f;      // TOT must be <= this (0 = disabled)
    float minThrottle    = 0.0f;      // throttle demand must be >= this (0 = disabled)

    const char* name() override { return "ABCheckReady"; }

    void onEnter() override {}

    BlockResult tick() override {
        auto& ed = EngineData::instance();

        // Bench mode: skip all pre-checks — no real sensors or throttle position available
        if (ed.benchMode) {
            Serial.println("[AB] CheckReady: BENCH — skipping pre-checks");
            return BlockResult::Complete;
        }

        // Must be in RUNNING mode
        if (ed.mode != SysMode::RUNNING) return BlockResult::Abort;

        // A configured N1 ignition window must be enforceable.
        if ((minN1 > 0 || maxN1 > 0) &&
            (!HardwareConfig::hasN1Rpm || !ed.n1Healthy)) {
            Serial.println("[AB] CheckReady abort: N1 sensor unavailable");
            return BlockResult::Abort;
        }

        // N1 floor
        if (minN1 > 0 && ed.n1Rpm < minN1) {
            Serial.printf("[AB] CheckReady abort: N1 too low (%.0f < %.0f)\n",
                          (double)ed.n1Rpm, (double)minN1);
            return BlockResult::Abort;
        }

        // N1 ceiling (compressor too fast — AB won't light)
        if (maxN1 > 0 && ed.n1Rpm > maxN1) {
            Serial.printf("[AB] CheckReady abort: N1 too high (%.0f > %.0f)\n",
                          (double)ed.n1Rpm, (double)maxN1);
            return BlockResult::Abort;
        }

        // TOT must be below the "too hot to light" limit
        if (maxTotForLight > 0 &&
            (!HardwareConfig::hasTot || !ed.totHealthy)) {
            Serial.println("[AB] CheckReady abort: TOT sensor unavailable");
            return BlockResult::Abort;
        }
        if (maxTotForLight > 0 && ed.tot > maxTotForLight) {
            Serial.printf("[AB] CheckReady abort: TOT too high for light (%.1f > %.1f)\n",
                          (double)ed.tot, (double)maxTotForLight);
            return BlockResult::Abort;
        }

        // Throttle minimum (makes no sense to fire AB at idle)
        if (minThrottle > 0 && ed.throttleDemand < minThrottle) {
            Serial.printf("[AB] CheckReady abort: throttle below min (%.2f < %.2f)\n",
                          (double)ed.throttleDemand, (double)minThrottle);
            return BlockResult::Abort;
        }

        Serial.println("[AB] CheckReady: all conditions met");
        return BlockResult::Complete;
    }

    void onExit() override {}
};
