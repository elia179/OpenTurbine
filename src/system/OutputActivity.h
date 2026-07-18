#pragma once

#include "HardwareConfig.h"
#include "Config.h"
#include "../engine/EngineData.h"
#include <Arduino.h>
#include <string.h>

// Canonical logical demand lookup for every physical output. Core actuators
// use EngineData's authoritative demand; non-core channels use the registry
// demand array. Web/OTA/START gates must use this instead of maintaining a
// second, legacy-only list of outputs.
namespace OutputActivity {
    inline float logicalDemand(const ChannelRegistry::Channel& c, uint8_t index,
                               const EngineData& ed) {
        const char* p = c.purpose;
        if (!strcmp(p, "main_fuel"))       return ed.throttleDemand;
        if (!strcmp(p, "fuel_shutoff"))    return ed.fuelSolOpen ? 1.0f : 0.0f;
        if (!strcmp(p, "starter"))         return ed.starterDemand;
        if (!strcmp(p, "starter_enable"))  return ed.starterEnabled ? 1.0f : 0.0f;
        if (!strcmp(p, "oil_pump"))        return ed.oilPumpPct / 100.0f;
        if (!strcmp(p, "scavenge_pump"))   return ed.oilScavengeDemand;
        if (!strcmp(p, "cooling_fan"))     return ed.coolFanDemand;
        if (!strcmp(p, "fuel_pump"))       return ed.fuelPump2Demand;
        if (!strcmp(p, "igniter"))         return ed.igniterOn ? 1.0f : 0.0f;
        if (!strcmp(p, "ab_igniter"))      return ed.igniter2On ? 1.0f : 0.0f;
        if (!strcmp(p, "ab_valve"))        return ed.abSolOpen ? 1.0f : 0.0f;
        if (!strcmp(p, "ab_pump"))         return ed.abPumpDemand;
        if (!strcmp(p, "prop_pitch"))      return ed.propPitchDemand;
        if (!strcmp(p, "air_starter"))     return ed.airstarterOpen ? 1.0f : 0.0f;
        if (!strcmp(p, "bleed_valve"))     return ed.bleedValveDemand;
        if (!strcmp(p, "glow_plug"))       return ed.glowPlugDemand;
        if (!strcmp(p, "wet_glow_fuel"))   return ed.wetGlowFuelDemand;
        return index < ChannelRegistry::MAX_OUTPUT_CHANNELS ? ed.registryOutputDemand[index] : 0.0f;
    }

    inline bool anyPhysicalDemand(bool allowStandbyOilFeed = false) {
        const auto& ed = EngineData::instance();
        const auto& reg = HardwareConfig::channelRegistry;
        for (uint8_t i = 0; i < reg.outputCount; ++i) {
            const auto& c = reg.outputs[i];
            if (!c.installed || c.pin < 0) continue;
            const float demand = logicalDemand(c, i, ed);
            if (demand <= 0.001f) continue;
            if (allowStandbyOilFeed && ed.standbyOilFeedActive && !strcmp(c.purpose, "oil_pump") &&
                demand <= constrain(Config::standbyOilFeedPct / 100.0f + 0.005f, 0.0f, 1.0f)) continue;
            return true;
        }
        return ed.extraCooldownActive || (ed.standbyOilFeedActive && !allowStandbyOilFeed);
    }
}
