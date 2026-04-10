#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/Config.h"
#include <Arduino.h>

// ============================================================
//  ABIgnite — fires the afterburner ignition
//
//  useTorch   : spike main fuel pump (fuelPumpDemand) for torchDurationMs
//  useIgniter : fire igniter2On for torchDurationMs
//  Both may be true simultaneously.
//
//  If torchTotLimit > 0 and TOT exceeds it, torch is cut early.
//  AB solenoid is NOT opened here — sequence designer places ABSolOpen
//  before this block if required.
// ============================================================

class ABIgnite : public IBlock {
public:
    bool  useTorch        = true;    // spike main fuel through turbine
    bool  useIgniter      = false;   // fire AB igniter (igniter2)
    float torchSpikePct   = 30.0f;   // extra fuel pump demand % (0–100)
    int   torchDurationMs = 400;
    float torchTotLimit   = 0.0f;    // safety TOT cap during torch (0=disabled)

    const char* name() override { return "ABIgnite"; }

    void onEnter() override {
        _startMs         = millis();
        _baseThrottle    = EngineData::instance().throttleDemand;
        _done            = false;

        auto& ed = EngineData::instance();

        // Safety: if torchTotLimit is 0 (disabled), skip torch entirely —
        // running without a TOT cap during AB ignition is unsafe.
        if (useTorch && torchTotLimit == 0.0f) {
            Serial.println("[AB] Ignite: torch skipped — torchTotLimit is 0 (no TOT safety cap configured)");
            useTorch = false;
        }

        // Torch: temporarily boost main fuel demand
        if (useTorch) {
            float requestedPct = _baseThrottle * 100.0f + torchSpikePct;
            float spike = constrain(requestedPct / 100.0f, 0.0f, 1.0f);
            ed.throttleDemand = spike;
            // Log the requested %, not the clamped result, so the user can see when
            // torchSpikePct is too large for the current throttle position.
            if (requestedPct > 100.0f) {
                Serial.printf("[AB] Ignite: torch spike %.0f%% (clamped to 100%%) for %d ms\n",
                              (double)requestedPct, torchDurationMs);
            } else {
                Serial.printf("[AB] Ignite: torch spike %.0f%% for %d ms\n",
                              (double)requestedPct, torchDurationMs);
            }
        }
        // Igniter: fire AB igniter (igniter2)
        if (useIgniter) {
            ed.igniter2On = true;
            Serial.println("[AB] Ignite: AB igniter ON");
        }
    }

    BlockResult tick() override {
        if (_done) return BlockResult::Complete;

        auto& ed = EngineData::instance();
        unsigned long elapsed = millis() - _startMs;

        // Safety: cut torch if TOT is getting too hot
        if (torchTotLimit > 0 && ed.totHealthy && ed.tot > torchTotLimit) {
            Serial.printf("[AB] Ignite: torch cut — TOT %.1f > limit %.1f\n",
                          (double)ed.tot, (double)torchTotLimit);
            _cutTorch(ed);
            _done = true;
            return BlockResult::Complete;
        }

        if (elapsed >= (unsigned long)torchDurationMs) {
            _cutTorch(ed);
            _done = true;
            return BlockResult::Complete;
        }

        return BlockResult::Running;
    }

    void onExit() override {
        // Ensure torch is cut on any exit path
        auto& ed = EngineData::instance();
        _cutTorch(ed);
    }

private:
    unsigned long _startMs      = 0;
    float         _baseThrottle = 0;
    bool          _done         = false;

    void _cutTorch(EngineData& ed) {
        if (useTorch) {
            ed.throttleDemand = _baseThrottle;  // restore pre-torch demand
        }
        // Do NOT cut igniter2 here — ABFlameConfirm may still want it on (mode=igniter)
        // Igniter is cut by ABIgnOff block or by ABFlameConfirm on exit
    }
};
