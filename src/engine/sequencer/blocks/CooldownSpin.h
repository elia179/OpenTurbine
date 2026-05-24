#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/Config.h"
#include "../../../system/FlightRecorder.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// Spin starter motor and/or run oil pump to cool EGT below safe storage temperature.
// Exits when TOT < target OR timeout.
// If oil pressure sensor is configured, drives the pump via a simple P-controller
// targeting oilPressureTarget bar instead of a fixed percentage.
class CooldownSpin : public IBlock {
public:
    float         totTarget          = 150.0f;
    float         starterCoolPct     = 0.40f;  // 40% starter speed
    float         oilCoolPct         = 30.0f;  // direct oil pump % (no pressure sensor)
    float         oilPressureTarget  = 2.0f;   // bar target (used when oil pressure sensor present)
    unsigned long timeoutMs          = 200000; // 3.3 min max
    bool          useScavengePump = false;   // also run scavenge pump during cooldown

    const char* name() override { return "CooldownSpin"; }

    void onEnter() override {
        _entryMs       = millis();
        _oilWarnLogged = false;
        auto& ed = EngineData::instance();

        // Skip immediately if fuel was never opened (no combustion = no hot EGT to cool)
        // Also skip in bench mode — no real heat was generated, no need to wait
        if (!ed.fuelEverOpened || ed.benchMode) {
            _skip = true;
            return;
        }
        _skip = false;

        if (Config::cooldownUseStarter) {
            ed.starterEnabled = true;
            ed.starterDemand  = starterCoolPct;
        }
        if (Config::cooldownUseOilPump) {
            // Start at oilCoolPct regardless of sensor presence.
            // With hasOilPress: tick() regulates up/down via P-controller from this seed.
            // Without hasOilPress: stays fixed at oilCoolPct for the whole cooldown.
            ed.oilPumpPct = oilCoolPct;
        }
        if (useScavengePump) ed.oilScavengeOn = true;
        ed.clusterCode = 11;    // ClCode::CooldownRunning
    }

    BlockResult tick() override {
        if (_skip) return BlockResult::Complete;
        auto& ed = EngineData::instance();

        // Pressure-fed oil system: regulate pump to target pressure
        if (HardwareConfig::hasOilPress && Config::cooldownUseOilPump) {
            float err = oilPressureTarget - ed.oilPressure;
            float adj = constrain(err * 0.15f, -5.0f, 5.0f);
            ed.oilPumpPct = constrain(ed.oilPumpPct + adj, 5.0f, 100.0f);
        }

        // Oil pump fail-check: if oil is near zero while pump is supposed to be running,
        // log a warning but do NOT abort — the engine must still cool regardless.
        if (HardwareConfig::hasOilPress && Config::cooldownUseOilPump
            && ed.oilHealthy && ed.oilPressure < Config::oilZeroBar
            && !_oilWarnLogged)
        {
            FlightRecorder::logAbort("CooldownSpin", "oil_pressure_zero_during_cooldown");
            Serial.println("[CooldownSpin] WARNING: oil pressure near zero — check oil pump");
            _oilWarnLogged = true;
        }

        if (ed.totHealthy && ed.tot < totTarget) return BlockResult::Complete;
        if ((millis() - _entryMs) > timeoutMs)   return BlockResult::Complete;
        return BlockResult::Running;
    }

    void onExit() override {
        auto& ed = EngineData::instance();
        ed.starterDemand  = 0;
        ed.starterEnabled = false;
        ed.oilPumpPct   = 0;
        ed.oilScavengeOn  = false;
    }

private:
    unsigned long _entryMs       = 0;
    bool          _skip          = false;
    bool          _oilWarnLogged = false;
};
