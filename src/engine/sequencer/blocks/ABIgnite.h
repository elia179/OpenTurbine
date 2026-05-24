#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/Config.h"
#include <Arduino.h>

// ============================================================
//  ABIgnite — fires the afterburner ignition
//
//  useTorch   : spike main fuel via abFuelOffset for torchDurationMs
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
        _done            = false;

        auto& ed = EngineData::instance();

        // Safety: if torchTotLimit is 0, skip torch for this attempt.
        // Use a local flag — do NOT mutate useTorch, which is a config member
        // set by applyConfig(). Mutating it would permanently disable torch
        // for all subsequent AB ignition attempts in this run.
        _doTorch = useTorch && (torchTotLimit > 0.0f);
        if (useTorch && torchTotLimit == 0.0f) {
            Serial.println("[AB] Ignite: torch skipped — torchTotLimit is 0 (no TOT safety cap configured)");
        }

        // Torch: temporarily boost main fuel via ed.abFuelOffset.
        // The offset is applied at the throttle actuator write in
        // Hardware::updateActuators(), NOT to throttleDemand itself.
        // Writing to throttleDemand would be overwritten by the pilot
        // throttle input on the very same tick (runControllers runs after
        // the AB sequencer), making the spike completely ineffective on
        // physical-throttle setups.
        if (_doTorch) {
            ed.abFuelOffset = torchSpikePct / 100.0f;
            Serial.printf("[AB] Ignite: torch spike +%.0f%% for %d ms\n",
                          (double)torchSpikePct, torchDurationMs);
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
        if (_doTorch && torchTotLimit > 0 && ed.totHealthy && ed.tot > torchTotLimit) {
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
    unsigned long _startMs  = 0;
    bool          _done     = false;
    bool          _doTorch  = false;  // runtime copy of useTorch — never mutates the config member

    void _cutTorch(EngineData& ed) {
        if (_doTorch) {
            ed.abFuelOffset = 0.0f;  // remove torch boost from actuator output
        }
        // Do NOT cut igniter2 here — ABFlameConfirm may still want it on (mode=igniter)
        // Igniter is cut by ABIgnOff block or by ABFlameConfirm on exit
    }
};
