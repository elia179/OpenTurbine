#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include <Arduino.h>

// ============================================================
//  ABFlameConfirm — waits for afterburner flame confirmation
//
//  flameMode:
//    0 = sensor    : wait for ed.abFlameOn (dedicated AB flame sensor)
//    1 = TOT_rise  : wait for tot to rise >= totRiseDegC within totRiseWindowMs
//    2 = timed     : wait assumeIgnitedMs then assume lit
//
//  If flameTimeoutMs elapses without confirmation → Fault.
//  Clears igniter2On on exit regardless of mode.
// ============================================================

class ABFlameConfirm : public IBlock {
public:
    int   flameMode        = 2;       // 0=sensor, 1=TOT_rise, 2=timed
    float totRiseDegC      = 30.0f;   // required TOT rise (mode 1)
    int   totRiseWindowMs  = 2000;    // window for TOT rise (mode 1)
    int   assumeIgnitedMs  = 1500;    // timed mode delay (mode 2)
    int   flameTimeoutMs   = 3000;    // overall timeout → Fault

    const char* name() override { return "ABFlameConfirm"; }

    void onEnter() override {
        _startMs     = millis();
        _totBaseline = EngineData::instance().tot;
        Serial.printf("[AB] FlameConfirm: mode=%d timeout=%d ms\n", flameMode, flameTimeoutMs);
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();
        unsigned long now     = millis();
        unsigned long elapsed = now - _startMs;

        // Bench mode: simulate confirmed AB flame after assumeIgnitedMs — no real sensor needed.
        // Uses the timed path regardless of flameMode so mode 0/1 don't fault on timeout.
        if (ed.benchMode) {
            if (elapsed >= (unsigned long)assumeIgnitedMs) {
                Serial.println("[AB] FlameConfirm: BENCH — simulating AB flame confirmed");
                return BlockResult::Complete;
            }
            return BlockResult::Running;
        }

        // Overall timeout → fault (ignition failed)
        if (elapsed > (unsigned long)flameTimeoutMs) {
            Serial.println("[AB] FlameConfirm: TIMEOUT — ignition failed");
            return BlockResult::Fault;
        }

        switch (flameMode) {
            case 0: // dedicated sensor
                if (ed.abFlameOn) {
                    Serial.println("[AB] FlameConfirm: sensor detected flame");
                    return BlockResult::Complete;
                }
                break;

            case 1: // TOT rise
            {
                // A TOT-dependent confirmation cannot be trusted without its sensor.
                if (!ed.totHealthy) {
                    Serial.println("[AB] FlameConfirm fault: TOT sensor unavailable");
                    return BlockResult::Fault;
                }
                float rise = ed.tot - _totBaseline;
                if (rise >= totRiseDegC) {
                    Serial.printf("[AB] FlameConfirm: TOT rose %.1f °C — confirmed\n",
                                  (double)rise);
                    return BlockResult::Complete;
                }
                // Baseline is fixed at onEnter snapshot — no per-tick ratcheting.
                // Per-sample updates caused noise sensitivity: a momentary dip in
                // the first window would lower the baseline and make the threshold
                // easier to trigger spuriously on sensor noise.
                break;
            }

            case 2: // timed assumption
                if (elapsed >= (unsigned long)assumeIgnitedMs) {
                    Serial.printf("[AB] FlameConfirm: timed — assuming lit after %d ms\n",
                                  assumeIgnitedMs);
                    return BlockResult::Complete;
                }
                break;

            default:
                return BlockResult::Complete;
        }

        return BlockResult::Running;
    }

    void onExit() override {
        // Cut AB igniter regardless of mode
        EngineData::instance().igniter2On = false;
    }

private:
    unsigned long _startMs    = 0;
    float         _totBaseline= 0;
};
