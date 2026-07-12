#pragma once
#include "HardwareConfig.h"

// One backend source for feature availability. Browser pages consume its JSON
// response rather than maintaining independent `hasX` matrices.
class HardwareCapabilities {
public:
    static bool hasInputRole(const char* role) { return hasRole(ChannelRegistry::INPUT, role); }
    static bool hasOutputRole(const char* role) { return hasRole(ChannelRegistry::OUTPUT, role); }
    static bool available(const char* feature) {
        if (!strcmp(feature, "oil_loop")) return hasInputRole("pressure") && hasOutputRole("oil_pump");
        if (!strcmp(feature, "n1_safety")) return hasBindingOrRole("primary_n1", "speed");
        if (!strcmp(feature, "n2_governor")) return hasBindingOrRole("primary_n2", "speed") && hasOutputRole("fuel");
        if (!strcmp(feature, "egt_safety")) return hasBindingOrRole("primary_egt", "temperature");
        return false;
    }
    static void toJson(JsonObject root, const char* feature) {
        bool ok = available(feature); root["feature"] = feature; root["available"] = ok;
        JsonArray missing = root["missing"].to<JsonArray>();
        if (ok) return;
        if (!strcmp(feature, "oil_loop")) { if (!hasInputRole("pressure")) missing.add("oil_pressure_input"); if (!hasOutputRole("oil_pump")) missing.add("oil_pump_output"); }
        else if (!strcmp(feature, "n1_safety")) missing.add("primary_n1");
        else if (!strcmp(feature, "n2_governor")) { if (!hasBindingOrRole("primary_n2", "speed")) missing.add("primary_n2"); if (!hasOutputRole("fuel")) missing.add("main_fuel_output"); }
        else if (!strcmp(feature, "egt_safety")) missing.add("primary_egt");
    }
private:
    static bool hasRole(ChannelRegistry::Direction direction, const char* role) {
        const ChannelRegistry& r = HardwareConfig::channelRegistry;
        const ChannelRegistry::Channel* list = direction == ChannelRegistry::INPUT ? r.inputs : r.outputs;
        uint8_t n = direction == ChannelRegistry::INPUT ? r.inputCount : r.outputCount;
        for (uint8_t i=0;i<n;i++) if (list[i].installed && !strcmp(list[i].role, role)) return true;
        return false;
    }
    static bool hasBindingOrRole(const char* key, const char* role) {
        const ChannelRegistry& r = HardwareConfig::channelRegistry;
        for (uint8_t i=0;i<r.bindingCount;i++) if (!strcmp(r.bindings[i].key,key) && (r.find(r.bindings[i].channelId, ChannelRegistry::INPUT) || r.find(r.bindings[i].channelId, ChannelRegistry::OUTPUT))) return true;
        return hasInputRole(role);
    }
};
