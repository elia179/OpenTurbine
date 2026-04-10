#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/Config.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  AdvancedBlocks — sequence blocks for expanded hardware support
//
//  BleedOpen / BleedClose — compressor bleed valve control.
//    Used during unloaded start (open=less surge risk) or surge
//    prevention.  One-shot blocks — complete in a single tick.
//
//  GlowPreheat — ramps glow plug power up from 0 → maxPct
//    over preheatMs, then holds at holdPct.  Used as the first
//    sequence block before fuel delivery to pre-heat the pilot
//    flame element.  Completes when preheat ramp finishes.
//
//  FuelPumpRamp — ramps fuelPump2Demand from startPct → endPct
//    over rampMs.  Useful for turboshaft fuel-metering systems
//    where the secondary pump follows a pre-programmed curve.
//
//  FuelPump2Set — sets fuelPump2Demand to a fixed % in one tick.
//    Simple companion to FuelPumpRamp for known set-points.
//
//  GovernorHold — waits until N2 is within bandRpm of targetRpm
//    (or times out).  Use after transitioning to RUNNING to
//    confirm the power turbine governor has taken hold.
// ============================================================

// ── Bleed valve blocks ────────────────────────────────────────

class BleedOpen : public IBlock {
public:
    const char* name() override { return "BleedOpen"; }
    void onEnter() override { EngineData::instance().bleedValveOpen = true; }
    BlockResult tick() override { return BlockResult::Complete; }
    void onExit() override {}
};

class BleedClose : public IBlock {
public:
    const char* name() override { return "BleedClose"; }
    void onEnter() override { EngineData::instance().bleedValveOpen = false; }
    BlockResult tick() override { return BlockResult::Complete; }
    void onExit() override {}
};

// ── Glow plug preheat ramp ────────────────────────────────────

class GlowPreheat : public IBlock {
public:
    unsigned long preheatMs   = 10000;  // total ramp duration (ms)
    float         maxPct      = 80.0f;  // peak duty during ramp
    float         holdPct     = 30.0f;  // duty to hold after ramp
    bool          waitUntilHot = false;  // hold at holdPct until current sensor confirms plug is hot

    const char* name() override { return "GlowPreheat"; }

    void onEnter() override {
        _startMs = millis();
        // Always pull from Config so changes take effect without a reboot.
        // (applyConfig() copies these too, but onEnter is the authoritative source
        //  for blocks that aren't wired into Hardware::applyConfig.)
        preheatMs = (unsigned long)Config::glowPreheatMs;
        maxPct    = Config::glowPreheatMaxPct;
        holdPct   = Config::glowHoldPct;
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();
        unsigned long elapsed = millis() - _startMs;
        if (elapsed < preheatMs) {
            // Linear ramp 0 → maxPct
            float frac = (float)elapsed / (float)preheatMs;
            ed.glowPlugPct = frac * maxPct;
            return BlockResult::Running;
        }
        // Preheat done — hold at hold duty
        ed.glowPlugPct = holdPct;
        if (waitUntilHot && HardwareConfig::hasGlowCurrentSensor && !ed.glowPlugHot) {
            return BlockResult::Running;  // keep holding until current sensor confirms hot
        }
        return BlockResult::Complete;
    }

    void onExit() override {
        // Leave glowPlugPct at holdPct — engine needs heat during ignition
    }

private:
    unsigned long _startMs = 0;
};

// ── Fuel pump 2 ramp ──────────────────────────────────────────

class FuelPumpRamp : public IBlock {
public:
    float         startPct = 0.0f;    // starting demand (0–100 %)
    float         endPct   = 80.0f;   // ending demand (0–100 %)
    unsigned long rampMs   = 3000;    // ramp duration

    const char* name() override { return "FuelPumpRamp"; }

    void onEnter() override {
        _startMs = millis();
        EngineData::instance().fuelPump2Demand = startPct / 100.0f;
    }

    BlockResult tick() override {
        unsigned long elapsed = millis() - _startMs;
        if (elapsed >= rampMs) {
            EngineData::instance().fuelPump2Demand = endPct / 100.0f;
            return BlockResult::Complete;
        }
        float frac = (float)elapsed / (float)rampMs;
        EngineData::instance().fuelPump2Demand = (startPct + frac * (endPct - startPct)) / 100.0f;
        return BlockResult::Running;
    }

    void onExit() override {}

private:
    unsigned long _startMs = 0;
};

// ── Fuel pump 2 set point (one-shot) ─────────────────────────

class FuelPump2Set : public IBlock {
public:
    float demandPct = 0.0f;  // target % (0–100)

    const char* name() override { return "FuelPump2Set"; }
    void onEnter() override { EngineData::instance().fuelPump2Demand = demandPct / 100.0f; }
    BlockResult tick() override { return BlockResult::Complete; }
    void onExit() override {}
};

// ── Governor stabilisation hold ───────────────────────────────

class GovernorHold : public IBlock {
public:
    unsigned long timeoutMs = 10000;  // max wait for N2 to stabilise
    float         bandRpm   = 500.0f; // success when N2 within this of target

    const char* name() override { return "GovernorHold"; }

    void onEnter() override {
        _startMs = millis();
        if (bandRpm == 500.0f) bandRpm = Config::governorBandRpm;
    }

    BlockResult tick() override {
        if ((millis() - _startMs) >= timeoutMs) return BlockResult::Complete; // timeout = proceed
        auto& ed = EngineData::instance();
        if (!ed.n2Healthy) return BlockResult::Running;  // wait for sensor
        float targetRpm = Config::governorTargetRpm;
        if (targetRpm > 0 && fabsf(ed.n2Rpm - targetRpm) < bandRpm) {
            return BlockResult::Complete;
        }
        return BlockResult::Running;
    }

    void onExit() override {}

private:
    unsigned long _startMs = 0;
};
