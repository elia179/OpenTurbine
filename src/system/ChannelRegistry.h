#pragma once

// Bounded, allocation-free inventory used by configuration validation and
// runtime binding resolution.  IDs are configuration keys; labels are never
// used to resolve a channel.
#include <Arduino.h>
#include <ArduinoJson.h>

class ChannelRegistry {
public:
    // Sized for the classic ESP32 build, which has very little remaining
    // static DRAM after the web server and existing control subsystems. Six
    // of each direction still permits dual oil systems plus auxiliary IO.
    static constexpr uint8_t MAX_INPUT_CHANNELS = 6;
    static constexpr uint8_t MAX_OUTPUT_CHANNELS = 6;
    static constexpr uint8_t MAX_BINDINGS = 5;
    enum Direction : uint8_t { Input, Output };
    enum Driver : uint8_t { Digital, Analog, Pulse, RcPwm, Relay, Pwm, Servo };
    struct Channel {
        bool installed = false;
        Direction direction = Input;
        Driver driver = Digital;
        char id[16] = {};
        char name[16] = {};
        char role[14] = {"generic"};
        int8_t pin = -1;
        float minValue = 0.0f, maxValue = 1.0f;
        float safeDemand = 0.0f, faultDemand = 0.0f;
    };
    struct Binding { char key[24] = {}; char channelId[24] = {}; };

    Channel inputs[MAX_INPUT_CHANNELS] = {};
    Channel outputs[MAX_OUTPUT_CHANNELS] = {};
    Binding bindings[MAX_BINDINGS] = {};
    uint8_t inputCount = 0, outputCount = 0, bindingCount = 0;

    void clear() { *this = ChannelRegistry(); }
    const Channel* find(const char* id, Direction direction) const {
        const Channel* list = direction == Input ? inputs : outputs;
        uint8_t count = direction == Input ? inputCount : outputCount;
        for (uint8_t i = 0; i < count; ++i) if (!strcmp(list[i].id, id)) return &list[i];
        return nullptr;
    }
    Channel* findMutable(const char* id, Direction direction) {
        Channel* list = direction == Input ? inputs : outputs;
        uint8_t count = direction == Input ? inputCount : outputCount;
        for (uint8_t i = 0; i < count; ++i) if (!strcmp(list[i].id, id)) return &list[i];
        return nullptr;
    }
    bool add(const Channel& c) {
        if (!validId(c.id) || findMutable(c.id, Input) || findMutable(c.id, Output) || c.pin < -1) return false;
        Channel* list = c.direction == Input ? inputs : outputs;
        uint8_t& count = c.direction == Input ? inputCount : outputCount;
        uint8_t max = c.direction == Input ? MAX_INPUT_CHANNELS : MAX_OUTPUT_CHANNELS;
        if (count >= max || !driverMatches(c.direction, c.driver) || !demandsValid(c)) return false;
        for (uint8_t i=0; i<inputCount; ++i) if (c.pin >= 0 && inputs[i].pin == c.pin) return false;
        for (uint8_t i=0; i<outputCount; ++i) if (c.pin >= 0 && outputs[i].pin == c.pin) return false;
        list[count++] = c; return true;
    }
    bool validate() const {
        for (uint8_t i=0; i<inputCount; ++i) if (!validId(inputs[i].id) || !driverMatches(Input, inputs[i].driver)) return false;
        for (uint8_t i=0; i<outputCount; ++i) if (!validId(outputs[i].id) || !driverMatches(Output, outputs[i].driver) || !demandsValid(outputs[i])) return false;
        for (uint8_t i=0; i<inputCount; ++i) for (uint8_t j=0; j<outputCount; ++j) if (inputs[i].pin >= 0 && inputs[i].pin == outputs[j].pin) return false;
        for (uint8_t i=0; i<bindingCount; ++i) if (!validId(bindings[i].key) || !find(bindings[i].channelId, Input) && !find(bindings[i].channelId, Output)) return false;
        return true;
    }
    void toJson(JsonObject root) const {
        JsonArray in = root["inputs"].to<JsonArray>(), out = root["outputs"].to<JsonArray>(), bind = root["bindings"].to<JsonArray>();
        write(in, inputs, inputCount); write(out, outputs, outputCount);
        for (uint8_t i=0;i<bindingCount;i++) { JsonObject b=bind.add<JsonObject>(); b["key"]=bindings[i].key; b["channel"]=bindings[i].channelId; }
    }
    bool fromJson(JsonObjectConst root) {
        clear(); if (!read(root["inputs"], Input) || !read(root["outputs"], Output)) return false;
        for (JsonObjectConst b : root["bindings"].as<JsonArrayConst>()) { if (bindingCount >= MAX_BINDINGS) return false; Binding& x=bindings[bindingCount++]; strlcpy(x.key,b["key"]|"",sizeof(x.key)); strlcpy(x.channelId,b["channel"]|"",sizeof(x.channelId)); }
        return validate();
    }
    static bool validId(const char* id) { if (!id || !id[0] || strlen(id) >= 16) return false; for (;*id;++id) if (!(isalnum(*id)||*id=='_'||*id=='-')) return false; return true; }
private:
    static bool driverMatches(Direction d, Driver v) { return d == Input ? v <= RcPwm : v >= Relay; }
    static bool demandsValid(const Channel& c) { return c.safeDemand >= 0 && c.safeDemand <= 1 && c.faultDemand >= 0 && c.faultDemand <= 1 && c.maxValue >= c.minValue; }
    static void write(JsonArray a, const Channel* list, uint8_t n) { for(uint8_t i=0;i<n;i++) { const Channel& c=list[i]; JsonObject o=a.add<JsonObject>(); o["id"]=c.id;o["name"]=c.name;o["role"]=c.role;o["driver"]=(uint8_t)c.driver;o["pin"]=c.pin;o["min"]=c.minValue;o["max"]=c.maxValue;o["safe_demand"]=c.safeDemand;o["fault_demand"]=c.faultDemand; } }
    bool read(JsonVariantConst v, Direction d) { for(JsonObjectConst o:v.as<JsonArrayConst>()) { Channel c; c.direction=d;c.installed=true;strlcpy(c.id,o["id"]|"",sizeof(c.id));strlcpy(c.name,o["name"]|c.id,sizeof(c.name));strlcpy(c.role,o["role"]|"generic",sizeof(c.role));c.driver=(Driver)(o["driver"]|0);c.pin=o["pin"]|-1;c.minValue=o["min"]|0.0f;c.maxValue=o["max"]|1.0f;c.safeDemand=o["safe_demand"]|0.0f;c.faultDemand=o["fault_demand"]|0.0f;if(!add(c))return false;} return true; }
};
