#pragma once
#include "HardwareConfig.h"

// One backend source for feature availability. Browser pages consume its JSON
// response rather than maintaining independent `hasX` matrices.
class HardwareCapabilities {
public:
    static bool hasInputRole(const char* role) { return hasRole(ChannelRegistry::Input, role); }
    static bool hasOutputRole(const char* role) { return hasRole(ChannelRegistry::Output, role); }
    static bool available(const char* feature) {
        if (!strcmp(feature, "oil_loop"))
            return (hasInputRole("pressure") && hasOutputRole("oil_pump")) ||
                   (HardwareConfig::hasOilPress && HardwareConfig::hasOilPump);
        if (!strcmp(feature, "n1_safety"))
            return hasInputBindingOrRole("primary_n1", "speed") || HardwareConfig::hasN1Rpm;
        if (!strcmp(feature, "n2_governor"))
            return (hasInputBindingOrRole("primary_n2", "speed") &&
                    (hasOutputRole("fuel") || hasOutputRole("prop_pitch"))) ||
                   (HardwareConfig::hasN2Rpm && (HardwareConfig::hasThrottle || HardwareConfig::hasPropPitch));
        if (!strcmp(feature, "egt_safety"))
            return hasInputBindingOrRole("primary_egt", "temperature") ||
                   HardwareConfig::hasTot || HardwareConfig::hasTit;
        if (!strcmp(feature, "dynamic_idle"))
            return ((hasInputBindingOrRole("primary_n1", "speed") ||
                     hasInputBindingOrRole("primary_n2", "speed")) &&
                    hasOutputRole("fuel")) ||
                   ((HardwareConfig::hasN1Rpm || HardwareConfig::hasN2Rpm) && HardwareConfig::hasThrottle);
        return false;
    }
    static const char* enabledFeatureRejectReason() {
        if (HardwareConfig::hasOilLoop && !available("oil_loop"))
            return "Oil control loop requires an oil pressure input and oil pump output";
        if (HardwareConfig::hasDynamicIdle && !available("dynamic_idle"))
            return "Dynamic idle requires an RPM input and throttle output";
        if (HardwareConfig::hasGovernor && !available("n2_governor"))
            return "Governor requires N2 RPM feedback and a throttle or prop-pitch output";
        if ((HardwareConfig::safetyOverspeed || HardwareConfig::safetySurge) && !available("n1_safety"))
            return "Overspeed/surge safety requires N1 RPM feedback";
        if ((HardwareConfig::safetyOvertemp || HardwareConfig::safetyHotStart) && !available("egt_safety"))
            return "Temperature safety requires a selected EGT/TIT input";
        if ((HardwareConfig::safetyLowOil || HardwareConfig::safetyOilZero) &&
            !(hasInputRole("pressure") || HardwareConfig::hasOilPress))
            return "Oil pressure safety requires an oil pressure input";
        if (HardwareConfig::safetyTitOvertemp && !(hasInputRole("temperature") || HardwareConfig::hasTit))
            return "TIT safety requires a TIT/temperature input";
        if (HardwareConfig::safetyOilTempHigh && !(hasInputRole("oil_temperature") || HardwareConfig::hasOilTemp))
            return "Oil temperature safety requires an oil temperature input";
        if (HardwareConfig::safetyFuelPressLow && !(hasInputRole("fuel_pressure") || HardwareConfig::hasFuelPress))
            return "Fuel pressure safety requires a fuel pressure input";
        if (HardwareConfig::safetyBattLow && !(hasInputRole("voltage") || HardwareConfig::hasBattVoltage))
            return "Battery safety requires a voltage input";
        return nullptr;
    }
    static void toJson(JsonObject root, const char* feature) {
        bool ok = available(feature); root["feature"] = feature; root["available"] = ok;
        JsonArray missing = root["missing"].to<JsonArray>();
        if (ok) return;
        if (!strcmp(feature, "oil_loop")) { if (!hasInputRole("pressure")) missing.add("oil_pressure_input"); if (!hasOutputRole("oil_pump")) missing.add("oil_pump_output"); }
        else if (!strcmp(feature, "n1_safety")) missing.add("primary_n1");
        else if (!strcmp(feature, "n2_governor")) { if (!hasInputBindingOrRole("primary_n2", "speed")) missing.add("primary_n2"); if (!hasOutputRole("fuel") && !hasOutputRole("prop_pitch")) missing.add("governor_output"); }
        else if (!strcmp(feature, "egt_safety")) missing.add("primary_egt");
        else if (!strcmp(feature, "dynamic_idle")) { if (!hasInputRole("speed")) missing.add("rpm_input"); if (!hasOutputRole("fuel")) missing.add("throttle_output"); }
    }
private:
    static bool hasRole(ChannelRegistry::Direction direction, const char* role) {
        const ChannelRegistry& r = HardwareConfig::channelRegistry;
        const ChannelRegistry::Channel* list = direction == ChannelRegistry::Input ? r.inputs : r.outputs;
        uint8_t n = direction == ChannelRegistry::Input ? r.inputCount : r.outputCount;
        for (uint8_t i=0;i<n;i++) if (list[i].installed && !strcmp(list[i].role, role)) return true;
        return false;
    }
    static bool hasBindingOrRole(const char* key, const char* role) {
        return hasInputBindingOrRole(key, role) || hasOutputBindingOrRole(key, role);
    }
    static bool hasInputBindingOrRole(const char* key, const char* role) {
        const ChannelRegistry& r = HardwareConfig::channelRegistry;
        for (uint8_t i=0;i<r.bindingCount;i++) if (!strcmp(r.bindings[i].key,key) && r.find(r.bindings[i].channelId, ChannelRegistry::Input)) return true;
        return hasInputRole(role);
    }
    static bool hasOutputBindingOrRole(const char* key, const char* role) {
        const ChannelRegistry& r = HardwareConfig::channelRegistry;
        for (uint8_t i=0;i<r.bindingCount;i++) if (!strcmp(r.bindings[i].key,key) && r.find(r.bindings[i].channelId, ChannelRegistry::Output)) return true;
        return hasOutputRole(role);
    }
};
