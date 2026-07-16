#pragma once

// Bounded, allocation-free inventory used by configuration validation and
// runtime binding resolution.  IDs are configuration keys; labels are never
// used to resolve a channel.
#include <Arduino.h>
#include <ArduinoJson.h>

class ChannelRegistry {
public:
    // Both supported boards expose the same inventory and configuration
    // schema. Classic ESP32 stores its instance on the heap (see
    // HardwareConfig) instead of reducing functionality to save static DRAM.
    // Handles occupy 64..79 (outputs) and 80..95 (inputs), leaving the fixed
    // rule/sequence handle ranges untouched.
    static constexpr uint8_t MAX_INPUT_CHANNELS = 16;
    static constexpr uint8_t MAX_OUTPUT_CHANNELS = 16;
    static constexpr uint8_t MAX_BINDINGS = 8;
    static constexpr uint8_t INPUT_SENSOR_BASE = 80;
    static constexpr uint8_t OUTPUT_ACTUATOR_BASE = 64;
    static constexpr bool isInputSensor(uint8_t handle) {
        return handle >= INPUT_SENSOR_BASE &&
               handle < INPUT_SENSOR_BASE + MAX_INPUT_CHANNELS;
    }
    static constexpr uint8_t inputIndexFromSensor(uint8_t handle) {
        return handle - INPUT_SENSOR_BASE;
    }
    static constexpr bool isOutputActuator(uint8_t handle) {
        return handle >= OUTPUT_ACTUATOR_BASE &&
               handle < OUTPUT_ACTUATOR_BASE + MAX_OUTPUT_CHANNELS;
    }
    static constexpr uint8_t outputIndexFromActuator(uint8_t handle) {
        return handle - OUTPUT_ACTUATOR_BASE;
    }
    static bool isCoreManagedOutputId(const char* id) {
        return id && (!strcmp(id, "main_fuel_output") ||
                      !strcmp(id, "main_fuel") ||
                      !strcmp(id, "main_starter") ||
                      !strcmp(id, "starter") ||
                      !strcmp(id, "starter_main") ||
                      !strcmp(id, "starter_enable") ||
                      !strcmp(id, "oil_pump_main") ||
                      !strcmp(id, "oil_pump") ||
                      !strcmp(id, "cooling_fan_main") ||
                      !strcmp(id, "cooling_fan") ||
                      !strcmp(id, "oil_scavenge_main") ||
                      !strcmp(id, "scavenge_pump") ||
                      !strcmp(id, "bleed_valve_main") ||
                      !strcmp(id, "bleed_valve") ||
                      !strcmp(id, "igniter") ||
                      !strcmp(id, "ab_igniter") ||
                      !strcmp(id, "igniter2_main") ||
                      !strcmp(id, "main_fuel_shutoff") ||
                      !strcmp(id, "fuel_shutoff") ||
                      !strcmp(id, "ab_solenoid") ||
                      !strcmp(id, "air_starter") ||
                      !strcmp(id, "fuel_pump") ||
                      !strcmp(id, "ab_pump") ||
                      !strcmp(id, "prop_pitch") ||
                      !strcmp(id, "glow_plug"));
    }
    static bool isCoreManagedInputId(const char* id) {
        return id && (!strcmp(id, "n1_main") ||
                      !strcmp(id, "n2_main") ||
                      !strcmp(id, "tot_main") ||
                      !strcmp(id, "primary_n1") ||
                      !strcmp(id, "primary_n2") ||
                      !strcmp(id, "primary_egt") ||
                      !strcmp(id, "oil_pressure_main") ||
                      !strcmp(id, "p1_main") ||
                      !strcmp(id, "p2_main") ||
                      !strcmp(id, "operator_throttle") ||
                      !strcmp(id, "operator_idle") ||
                      !strcmp(id, "battery_voltage") ||
                      !strcmp(id, "batt_voltage_main"));
    }
    static bool isCoreManagedInputPurpose(const char* purpose) {
        return purpose && (!strcmp(purpose, "n1_speed") || !strcmp(purpose, "n2_speed") ||
                           !strcmp(purpose, "tot") || !strcmp(purpose, "tit") ||
                           !strcmp(purpose, "oil_pressure") || !strcmp(purpose, "oil_temperature") ||
                           !strcmp(purpose, "fuel_pressure") || !strcmp(purpose, "p1_pressure") ||
                           !strcmp(purpose, "p2_pressure") || !strcmp(purpose, "fuel_flow") ||
                           !strcmp(purpose, "flame") || !strcmp(purpose, "torque") ||
                           !strcmp(purpose, "battery_voltage") || !strcmp(purpose, "throttle") ||
                           !strcmp(purpose, "idle"));
    }
    static bool isCoreOutputBindingKey(const char* key) {
        return key && (!strcmp(key, "main_fuel_output") ||
                       !strcmp(key, "main_fuel_shutoff") ||
                       !strcmp(key, "main_starter"));
    }
    static bool isCoreManagedOutputRole(const char* role) {
        (void)role;
        return false;
    }
    static bool isCoreManagedOutputPurpose(const char* purpose) {
        return purpose && (!strcmp(purpose, "main_fuel") ||
                           !strcmp(purpose, "fuel_shutoff") ||
                           !strcmp(purpose, "starter") ||
                           !strcmp(purpose, "starter_enable") ||
                           !strcmp(purpose, "oil_pump") ||
                           !strcmp(purpose, "scavenge_pump") ||
                           !strcmp(purpose, "cooling_fan") ||
                           !strcmp(purpose, "fuel_pump") ||
                           !strcmp(purpose, "igniter") ||
                           !strcmp(purpose, "ab_igniter") ||
                           !strcmp(purpose, "ab_valve") ||
                           !strcmp(purpose, "glow_plug") ||
                           !strcmp(purpose, "ab_pump") ||
                           !strcmp(purpose, "prop_pitch") ||
                           !strcmp(purpose, "air_starter"));
    }
    static bool isDedicatedTemperatureId(const char* id) {
        return id && (!strcmp(id, "tot_main") || !strcmp(id, "tit_main") ||
                      !strcmp(id, "oil_temperature") || !strcmp(id, "coolant_temperature"));
    }
    enum Direction : uint8_t { Input, Output };
    static bool roleValid(Direction d, const char* role) {
        if (!role || !role[0] || strlen(role) >= 18) return false;
        if (!strcmp(role, "generic")) return true;
        if (d == Input) {
            return !strcmp(role, "speed") ||
                   !strcmp(role, "pressure") ||
                   !strcmp(role, "temperature") ||
                   !strcmp(role, "flame") ||
                   !strcmp(role, "flow") ||
                   !strcmp(role, "torque") ||
                   !strcmp(role, "voltage") ||
                   !strcmp(role, "operator") ||
                   !strcmp(role, "digital_switch") ||
                   !strcmp(role, "fault") ||
                   !strcmp(role, "estop") ||
                   !strcmp(role, "inhibit_start") ||
                   !strcmp(role, "low_oil_switch") ||
                   !strcmp(role, "oil_zero_switch") ||
                   !strcmp(role, "sequence_gate") ||
                   !strcmp(role, "ab_arm") ||
                   !strcmp(role, "ab_fire") ||
                   !strcmp(role, "limp_mode");
        }
        return !strcmp(role, "fuel") ||
               !strcmp(role, "fuel_shutoff") ||
               !strcmp(role, "starter") ||
               !strcmp(role, "starter_en") ||
               !strcmp(role, "oil_pump") ||
               !strcmp(role, "coolant_pump") ||
               !strcmp(role, "scavenge_pump") ||
               !strcmp(role, "cooling_fan") ||
               !strcmp(role, "valve") ||
               !strcmp(role, "igniter") ||
               !strcmp(role, "ab_igniter") ||
               !strcmp(role, "glow_plug") ||
               !strcmp(role, "fuel_pump") ||
               !strcmp(role, "ab_pump") ||
               !strcmp(role, "prop_pitch");
    }
    // Driver numbers are persisted. Keep 0..6 stable and append new drivers.
    enum Driver : uint8_t { Digital, Analog, Pulse, RcPwm, Relay, Pwm, Servo, PwmDuty };
    struct Channel {
        bool installed = false;
        Direction direction = Input;
        Driver driver = Digital;
        char id[20] = {};
        char name[16] = {};
        char role[18] = {"generic"};
        char purpose[20] = {"generic"};
        int8_t pin = -1;
        float minValue = 0.0f, maxValue = 1.0f;
        float pulsesPerUnit = 1.0f; // pulse inputs: speed=pulses/rev, flow=pulses/litre
        float analogZeroMv = 0.0f;      // analog physical roles: mV at zero output
        float analogMvPerUnit = 1000.0f; // speed=RPM, pressure=bar, temp=C, flow=L/min, torque=Nm
        float analogDivider = 1.0f;     // voltage role: Vbatt = ADC volts * divider
        // Torque cards can use a normal analog transmitter (0) or an HX711
        // bridge ADC (1).  For HX711, pin is DOUT and hx711Clk is SCK.
        uint8_t torqueInterface = 0;
        int8_t hx711Clk = -1;
        float hx711Scale = 1.0f;
        int32_t hx711Zero = 0;
        // Temperature cards can be a calibrated analog transmitter (0), a
        // thermocouple amplifier (1=MAX6675, 2=MAX31855, 3=MAX31856), an
        // NTC divider (4), or a DS18B20 OneWire probe (5). SPI bus lines may
        // be shared; each thermocouple amplifier owns its CS pin.
        uint8_t temperatureInterface = 0;
        int8_t spiClk = -1, spiCs = -1, spiMiso = -1, spiMosi = -1;
        char tcType[2] = "K";
        uint8_t temperatureResolution = 12;
        float thermistorBeta = 3950.0f, thermistorR0 = 10000.0f, thermistorRFixed = 10000.0f;
        bool thermistorPullup = true;
        float safeDemand = 0.0f;
        bool forceSafeOnFault = false; // optional hard override during fault shutdown
        float minimumRunDemand = 0.0f; // operational floor inside the electrical output range
        uint32_t pwmFrequency = 5000;
        uint8_t pwmResolution = 10;
        bool pwmTimingConfigured = false;
        bool inverted = false;
        bool activeHigh = true;
        bool pullup = false;
        bool pulldown = false;
        bool hasCurrent = false;
        int8_t currentPin = -1;
        float currentMvPerA = 100.0f;
        float currentZeroV = 1.65f;
        float currentMaxAmps = 0.0f;
    };
    static bool isCoreManagedInput(const Channel& c) {
        return isCoreManagedInputId(c.id) || isCoreManagedInputPurpose(c.purpose);
    }
    struct Binding { char key[20] = {}; char channelId[20] = {}; };

    Channel inputs[MAX_INPUT_CHANNELS] = {};
    Channel outputs[MAX_OUTPUT_CHANNELS] = {};
    Binding bindings[MAX_BINDINGS] = {};
    uint8_t inputCount = 0, outputCount = 0, bindingCount = 0;

    // Only active entries are observable. Resetting the counts avoids
    // constructing a 5+ KB temporary registry on the Arduino loop stack.
    void clear() { inputCount = 0; outputCount = 0; bindingCount = 0; }
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
        const bool thermocouple = c.direction == Input && !strcmp(c.role, "temperature") &&
                                  c.temperatureInterface >= 1 && c.temperatureInterface <= 3;
        if (!validId(c.id) || findMutable(c.id, Input) || findMutable(c.id, Output) ||
            (!thermocouple && c.pin < 0) || (thermocouple && !temperatureInterfaceValid(c))) return false;
        Channel* list = c.direction == Input ? inputs : outputs;
        uint8_t& count = c.direction == Input ? inputCount : outputCount;
        uint8_t max = c.direction == Input ? MAX_INPUT_CHANNELS : MAX_OUTPUT_CHANNELS;
        if (count >= max || !driverMatches(c.direction, c.driver) || !roleValid(c.direction, c.role) ||
            !purposeValid(c.direction, c.purpose) || !semanticDriverValid(c) || !demandsValid(c)) return false;
        for (uint8_t i=0; i<inputCount; ++i) {
            if (c.pin >= 0 && (inputs[i].pin == c.pin || inputs[i].hx711Clk == c.pin)) return false;
            if (c.hx711Clk >= 0 && (inputs[i].pin == c.hx711Clk || inputs[i].hx711Clk == c.hx711Clk)) return false;
        }
        for (uint8_t i=0; i<outputCount; ++i) {
            if (c.pin >= 0 && (outputs[i].pin == c.pin || outputs[i].hx711Clk == c.pin)) return false;
            if (c.hx711Clk >= 0 && (outputs[i].pin == c.hx711Clk || outputs[i].hx711Clk == c.hx711Clk)) return false;
        }
        list[count] = c;
        if (!strcmp(list[count].purpose, "generic") && strcmp(list[count].role, "generic"))
            strlcpy(list[count].purpose, derivePurpose(c.direction, c.id, c.role), sizeof(list[count].purpose));
        count++;
        return true;
    }
    bool validate() const {
        for (uint8_t i=0; i<inputCount; ++i) if (!validId(inputs[i].id) || !driverMatches(Input, inputs[i].driver) || !roleValid(Input, inputs[i].role) || !purposeValid(Input, inputs[i].purpose) || !semanticDriverValid(inputs[i]) || !temperatureInterfaceValid(inputs[i]) || !torqueInterfaceValid(inputs[i]) || !demandsValid(inputs[i])) return false;
        for (uint8_t i=0; i<outputCount; ++i) if (!validId(outputs[i].id) || !driverMatches(Output, outputs[i].driver) || !roleValid(Output, outputs[i].role) || !purposeValid(Output, outputs[i].purpose) || !semanticDriverValid(outputs[i]) || !demandsValid(outputs[i])) return false;
        uint8_t auxiliaryPcnt = 0, registryOneWire = 0;
        for (uint8_t i=0; i<inputCount; ++i) {
            if (inputs[i].driver == Pulse && !strcmp(inputs[i].purpose, "shaft_speed")) auxiliaryPcnt++;
            if (inputs[i].temperatureInterface == 5 && strcmp(inputs[i].purpose, "oil_temperature")) registryOneWire++;
        }
        if (auxiliaryPcnt > 2 || registryOneWire > 4) return false;
        for (uint8_t i=0; i<inputCount; ++i) for (uint8_t j=0; j<outputCount; ++j) if (inputs[i].pin >= 0 && inputs[i].pin == outputs[j].pin) return false;
        for (uint8_t i=0; i<bindingCount; ++i) if (!bindingValid(bindings[i])) return false;
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
    bool boundToCoreOutput(const Channel& c) const {
        for (uint8_t i = 0; i < bindingCount; i++)
            if (isCoreOutputBindingKey(bindings[i].key) &&
                strcmp(bindings[i].channelId, c.id) == 0) return true;
        return false;
    }
    bool ownsCoreOutput(const Channel& c) const {
        if (isCoreManagedOutputId(c.id)) return true;
        if (!isCoreManagedOutputPurpose(c.purpose)) return false;
        // Prefer the migrated/canonical card when it exists. Otherwise the
        // first card for this purpose owns the legacy controller adapter and
        // later cards stay available to rules and sequences.
        for (uint8_t i = 0; i < outputCount; ++i)
            if (!strcmp(outputs[i].purpose, c.purpose) && isCoreManagedOutputId(outputs[i].id))
                return &outputs[i] == &c;
        for (uint8_t i = 0; i < outputCount; ++i)
            if (!strcmp(outputs[i].purpose, c.purpose)) return &outputs[i] == &c;
        return false;
    }
    static bool validId(const char* id) { if (!id || !id[0] || strlen(id) >= 20) return false; for (;*id;++id) if (!(isalnum(*id)||*id=='_'||*id=='-')) return false; return true; }
    static bool purposeValid(Direction d, const char* purpose) {
        if (!purpose || !purpose[0] || strlen(purpose) >= 20) return false;
        if (!strcmp(purpose, "generic")) return true;
        if (d == Input) {
            return !strcmp(purpose, "n1_speed") || !strcmp(purpose, "n2_speed") ||
                   !strcmp(purpose, "shaft_speed") || !strcmp(purpose, "tot") ||
                   !strcmp(purpose, "tit") || !strcmp(purpose, "oil_pressure") ||
                   !strcmp(purpose, "fuel_pressure") || !strcmp(purpose, "p1_pressure") ||
                   !strcmp(purpose, "p2_pressure") || !strcmp(purpose, "coolant_pressure") ||
                   !strcmp(purpose, "oil_temperature") || !strcmp(purpose, "coolant_temp") ||
                   !strcmp(purpose, "intake_temperature") || !strcmp(purpose, "fuel_flow") ||
                   !strcmp(purpose, "flame") || !strcmp(purpose, "torque") ||
                   !strcmp(purpose, "battery_voltage") || !strcmp(purpose, "throttle") ||
                   !strcmp(purpose, "idle") || !strcmp(purpose, "digital_switch") ||
                   !strcmp(purpose, "fault") || !strcmp(purpose, "estop") ||
                   !strcmp(purpose, "inhibit_start") || !strcmp(purpose, "sequence_gate") ||
                   !strcmp(purpose, "low_oil_switch") || !strcmp(purpose, "oil_zero_switch") ||
                   !strcmp(purpose, "ab_arm") || !strcmp(purpose, "ab_fire") ||
                   !strcmp(purpose, "limp_mode");
        }
        return !strcmp(purpose, "main_fuel") || !strcmp(purpose, "fuel_shutoff") ||
               !strcmp(purpose, "starter") || !strcmp(purpose, "starter_enable") ||
               !strcmp(purpose, "oil_pump") || !strcmp(purpose, "coolant_pump") ||
               !strcmp(purpose, "scavenge_pump") || !strcmp(purpose, "cooling_fan") ||
               !strcmp(purpose, "valve") || !strcmp(purpose, "igniter") ||
               !strcmp(purpose, "ab_igniter") || !strcmp(purpose, "glow_plug") ||
               !strcmp(purpose, "fuel_pump") || !strcmp(purpose, "ab_pump") ||
               !strcmp(purpose, "ab_valve") ||
               !strcmp(purpose, "prop_pitch") ||
               !strcmp(purpose, "air_starter") || !strcmp(purpose, "pilot_fuel") ||
               !strcmp(purpose, "purge_valve") || !strcmp(purpose, "nozzle_actuator");
    }
private:
    static bool driverMatches(Direction d, Driver v) {
        return d == Input
            ? (v == Digital || v == Analog || v == Pulse || v == RcPwm || v == PwmDuty)
            : (v == Relay || v == Pwm || v == Servo);
    }
    static bool semanticDriverValid(const Channel& c) {
        if (c.direction != Input) return true;
        if (!strcmp(c.role, "speed") || !strcmp(c.role, "flow"))
            return c.driver == Pulse || c.driver == Analog;
        if (!strcmp(c.role, "pressure") || !strcmp(c.role, "temperature") ||
            !strcmp(c.role, "torque") ||
            !strcmp(c.role, "voltage"))
            return c.driver == Analog;
        if (!strcmp(c.role, "flame")) return c.driver == Digital || c.driver == Analog;
        if (!strcmp(c.role, "operator")) return c.driver == Digital || c.driver == Analog ||
                                                 c.driver == Pulse || c.driver == RcPwm ||
                                                 c.driver == PwmDuty;
        if (!strcmp(c.role, "digital_switch") || !strcmp(c.role, "fault") ||
            !strcmp(c.role, "estop") || !strcmp(c.role, "inhibit_start") ||
            !strcmp(c.role, "low_oil_switch") || !strcmp(c.role, "oil_zero_switch") ||
            !strcmp(c.role, "sequence_gate") || !strcmp(c.role, "ab_arm") ||
            !strcmp(c.role, "ab_fire") || !strcmp(c.role, "limp_mode"))
            return c.driver == Digital;
        return true;
    }
    static bool temperatureInterfaceValid(const Channel& c) {
        if (!c.temperatureInterface) return true;
        if (c.direction != Input || strcmp(c.role, "temperature")) return false;
        if (c.temperatureInterface >= 1 && c.temperatureInterface <= 3) {
            // Thermocouple amplifiers remain limited to turbine-gas cards and
            // the existing oil-temperature compatibility path.
            const bool thermocouplePurpose = !strcmp(c.purpose, "tot") || !strcmp(c.purpose, "tit") ||
                                               !strcmp(c.purpose, "oil_temperature");
            if (!thermocouplePurpose) return false;
            if (c.spiClk < 0 || c.spiCs < 0 || c.spiMiso < 0) return false;
            return c.temperatureInterface != 3 || c.spiMosi >= 0;
        }
        if (c.temperatureInterface == 4)
            return c.pin >= 0 && c.thermistorBeta > 0.0f && c.thermistorR0 > 0.0f && c.thermistorRFixed > 0.0f;
        return c.temperatureInterface == 5 && c.pin >= 0 &&
               c.temperatureResolution >= 9 && c.temperatureResolution <= 12;
    }
    static bool torqueInterfaceValid(const Channel& c) {
        if (!c.torqueInterface) return true;
        return c.torqueInterface == 1 && c.direction == Input && !strcmp(c.role, "torque") &&
               c.driver == Analog && c.pin >= 0 && c.hx711Clk >= 0 && c.pin != c.hx711Clk &&
               c.hx711Scale >= 0.000001f && c.hx711Scale <= 1000000.0f;
    }
    static bool rangeValid(const Channel& c) {
        if (c.torqueInterface == 1) return torqueInterfaceValid(c);
        if (c.maxValue < c.minValue) return false;
        if (c.driver == Analog) {
            if (c.minValue < 0.0f || c.maxValue > 4095.0f || c.maxValue <= c.minValue) return false;
            if (strcmp(c.role, "generic") && strcmp(c.role, "operator") && strcmp(c.role, "flame")) {
                if (!strcmp(c.role, "voltage")) return c.analogDivider >= 1.0f && c.analogDivider <= 100.0f;
                return c.analogMvPerUnit > 0.0f && c.analogMvPerUnit <= 1000000.0f;
            }
            return true;
        }
        if (c.driver == Pulse) return c.minValue >= 0.0f && c.maxValue > c.minValue && c.pulsesPerUnit > 0.0f;
        if (c.driver == RcPwm || c.driver == Servo) return c.minValue >= 500.0f && c.maxValue <= 2500.0f && c.maxValue > c.minValue;
        if (c.driver == PwmDuty) return c.minValue >= 0.0f && c.maxValue <= 1.0f && c.maxValue > c.minValue;
        if (c.driver == Pwm) return c.minValue >= 0.0f && c.maxValue <= 1.0f &&
                                    (!c.pwmTimingConfigured || (c.pwmFrequency >= 1 && c.pwmFrequency <= 100000 &&
                                     c.pwmResolution >= 8 && c.pwmResolution <= 14));
        return true;
    }
    static bool demandsValid(const Channel& c) {
        return c.safeDemand >= 0 && c.safeDemand <= 1 &&
               c.minimumRunDemand >= 0 && c.minimumRunDemand <= 1 &&
               !(c.pullup && c.pulldown) &&
               (!c.hasCurrent || (c.currentPin >= 0 && c.currentMvPerA > 0.0f &&
                                  c.currentZeroV >= 0.0f && c.currentZeroV <= 3.3f &&
                                  c.currentMaxAmps >= 0.0f)) &&
               rangeValid(c);
    }
    bool bindingValid(const Binding& b) const {
        if (!validId(b.key)) return false;
        Direction expected = Input;
        bool known = false;
        if (!strcmp(b.key, "primary_n1") || !strcmp(b.key, "primary_n2") ||
            !strcmp(b.key, "primary_egt") || !strcmp(b.key, "operator_throttle")) {
            expected = Input;
            known = true;
        } else if (!strcmp(b.key, "main_fuel_output") ||
                   !strcmp(b.key, "main_fuel_shutoff") ||
                   !strcmp(b.key, "main_starter")) {
            expected = Output;
            known = true;
        }
        if (known) {
            const Channel* c = find(b.channelId, expected);
            if (!c) return false;
            if (!strcmp(b.key, "primary_n1")) return !strcmp(c->purpose, "n1_speed");
            if (!strcmp(b.key, "primary_n2")) return !strcmp(c->purpose, "n2_speed");
            if (!strcmp(b.key, "primary_egt")) return !strcmp(c->purpose, "tot") || !strcmp(c->purpose, "tit");
            if (!strcmp(b.key, "operator_throttle")) return !strcmp(c->purpose, "throttle");
            if (!strcmp(b.key, "main_fuel_output")) return !strcmp(c->purpose, "main_fuel");
            if (!strcmp(b.key, "main_fuel_shutoff")) return !strcmp(c->purpose, "fuel_shutoff");
            if (!strcmp(b.key, "main_starter")) return !strcmp(c->purpose, "starter");
            return true;
        }
        return find(b.channelId, Input) || find(b.channelId, Output);
    }
    static const char* derivePurpose(Direction d, const char* id, const char* role) {
        if (d == Input) {
            if (!strcmp(id, "n1_main") || !strcmp(id, "primary_n1")) return "n1_speed";
            if (!strcmp(id, "n2_main") || !strcmp(id, "primary_n2")) return "n2_speed";
            if (!strcmp(id, "tot_main") || !strcmp(id, "primary_egt")) return "tot";
            if (!strcmp(id, "tit_main")) return "tit";
            if (!strcmp(id, "oil_pressure_main")) return "oil_pressure";
            if (!strcmp(id, "fuel_pressure")) return "fuel_pressure";
            if (!strcmp(id, "p1_main") || !strcmp(id, "p1")) return "p1_pressure";
            if (!strcmp(id, "p2_main") || !strcmp(id, "p2")) return "p2_pressure";
            if (!strcmp(id, "oil_temperature")) return "oil_temperature";
            if (!strcmp(id, "coolant_temperature")) return "coolant_temp";
            if (!strcmp(id, "intake_temperature")) return "intake_temperature";
            if (!strcmp(id, "coolant_pressure")) return "coolant_pressure";
            if (!strcmp(id, "fuel_flow") || !strcmp(id, "fuel_flow_main")) return "fuel_flow";
            if (!strcmp(id, "flame_main")) return "flame";
            if (!strcmp(id, "torque_main")) return "torque";
            if (!strcmp(id, "battery_voltage") || !strcmp(id, "batt_voltage_main")) return "battery_voltage";
            if (!strcmp(id, "operator_throttle")) return "throttle";
            if (!strcmp(id, "operator_idle")) return "idle";
            if (!strcmp(role, "fault") || !strcmp(role, "estop") ||
                !strcmp(role, "low_oil_switch") || !strcmp(role, "oil_zero_switch") ||
                !strcmp(role, "inhibit_start") || !strcmp(role, "sequence_gate") ||
                !strcmp(role, "ab_arm") || !strcmp(role, "ab_fire") ||
                !strcmp(role, "limp_mode")) return role;
            if (!strcmp(role, "digital_switch")) return "digital_switch";
            if (!strcmp(role, "speed")) return "shaft_speed";
            return "generic";
        }
        if (!strcmp(id, "main_fuel") || !strcmp(id, "main_fuel_output")) return "main_fuel";
        if (!strcmp(id, "fuel_shutoff") || !strcmp(id, "main_fuel_shutoff")) return "fuel_shutoff";
        if (!strcmp(id, "starter") || !strcmp(id, "starter_main") || !strcmp(id, "main_starter")) return "starter";
        if (!strcmp(id, "starter_enable")) return "starter_enable";
        if (!strcmp(id, "oil_pump") || !strcmp(id, "oil_pump_main")) return "oil_pump";
        if (!strcmp(id, "coolant_pump")) return "coolant_pump";
        if (!strcmp(id, "scavenge_pump") || !strcmp(id, "oil_scavenge_main")) return "scavenge_pump";
        if (!strcmp(id, "cooling_fan") || !strcmp(id, "cooling_fan_main")) return "cooling_fan";
        if (!strcmp(id, "igniter")) return "igniter";
        if (!strcmp(id, "ab_igniter") || !strcmp(id, "igniter2_main")) return "ab_igniter";
        if (!strcmp(id, "ab_solenoid")) return "ab_valve";
        if (!strcmp(id, "glow_plug")) return "glow_plug";
        if (!strcmp(id, "fuel_pump")) return "fuel_pump";
        if (!strcmp(id, "ab_pump")) return "ab_pump";
        if (!strcmp(id, "prop_pitch")) return "prop_pitch";
        if (!strcmp(id, "air_starter")) return "air_starter";
        if (!strcmp(id, "pilot_fuel")) return "pilot_fuel";
        if (!strcmp(id, "purge_valve")) return "purge_valve";
        if (!strcmp(id, "nozzle_actuator")) return "nozzle_actuator";
        if (!strcmp(role, "coolant_pump")) return "coolant_pump";
        if (!strcmp(role, "starter_en")) return "starter_enable";
        return roleValid(Output, role) && strcmp(role, "fuel") ? role : (!strcmp(role, "fuel") ? "main_fuel" : "generic");
    }
    static void write(JsonArray a, const Channel* list, uint8_t n) {
        for (uint8_t i = 0; i < n; i++) {
            const Channel& c = list[i]; JsonObject o = a.add<JsonObject>();
            o["id"] = c.id; o["name"] = c.name; o["role"] = c.role; o["purpose"] = c.purpose; o["driver"] = (uint8_t)c.driver; o["pin"] = c.pin;
            o["min"] = c.minValue; o["max"] = c.maxValue; o["pulses_per_unit"] = c.pulsesPerUnit;
            o["analog_zero_mv"] = c.analogZeroMv; o["analog_mv_per_unit"] = c.analogMvPerUnit; o["analog_divider"] = c.analogDivider;
            o["torque_interface"] = c.torqueInterface; o["hx711_clk"] = c.hx711Clk; o["hx711_scale"] = c.hx711Scale; o["hx711_zero"] = c.hx711Zero;
            o["temp_interface"] = c.temperatureInterface; o["spi_clk"] = c.spiClk; o["spi_cs"] = c.spiCs; o["spi_miso"] = c.spiMiso; o["spi_mosi"] = c.spiMosi; o["tc_type"] = c.tcType;
            o["temp_resolution"] = c.temperatureResolution; o["ntc_beta"] = c.thermistorBeta; o["ntc_r0"] = c.thermistorR0; o["ntc_r_fixed"] = c.thermistorRFixed; o["ntc_pullup"] = c.thermistorPullup;
            o["safe_demand"] = c.safeDemand;
            o["force_safe_on_fault"] = c.forceSafeOnFault;
            o["min_run_demand"] = c.minimumRunDemand;
            if (c.pwmTimingConfigured) { o["pwm_freq_hz"] = c.pwmFrequency; o["pwm_res_bits"] = c.pwmResolution; }
            o["invert"] = c.inverted; o["active_high"] = c.activeHigh; o["pullup"] = c.pullup; o["pulldown"] = c.pulldown;
            o["has_current"] = c.hasCurrent; o["current_pin"] = c.currentPin; o["current_mv_a"] = c.currentMvPerA; o["current_zero_v"] = c.currentZeroV; o["current_max_a"] = c.currentMaxAmps;
        }
    }
    bool read(JsonVariantConst v, Direction d) {
        for (JsonObjectConst o : v.as<JsonArrayConst>()) {
            Channel c; c.direction = d; c.installed = true;
            strlcpy(c.id, o["id"] | "", sizeof(c.id)); strlcpy(c.name, o["name"] | c.id, sizeof(c.name)); strlcpy(c.role, o["role"] | "generic", sizeof(c.role));
            strlcpy(c.purpose, o["purpose"] | derivePurpose(d, c.id, c.role), sizeof(c.purpose));
            c.driver = (Driver)(o["driver"] | 0); c.pin = o["pin"] | -1; c.minValue = o["min"] | 0.0f; c.maxValue = o["max"] | 1.0f;
            c.pulsesPerUnit = o["pulses_per_unit"] | 1.0f; c.analogZeroMv = o["analog_zero_mv"] | 0.0f; c.analogMvPerUnit = o["analog_mv_per_unit"] | 1000.0f; c.analogDivider = o["analog_divider"] | 1.0f;
            c.torqueInterface = o["torque_interface"] | 0; c.hx711Clk = o["hx711_clk"] | -1; c.hx711Scale = o["hx711_scale"] | 1.0f; c.hx711Zero = o["hx711_zero"] | 0;
            c.temperatureInterface = o["temp_interface"] | 0; c.spiClk = o["spi_clk"] | -1; c.spiCs = o["spi_cs"] | -1; c.spiMiso = o["spi_miso"] | -1; c.spiMosi = o["spi_mosi"] | -1; strlcpy(c.tcType, o["tc_type"] | "K", sizeof(c.tcType));
            c.temperatureResolution = o["temp_resolution"] | 12; c.thermistorBeta = o["ntc_beta"] | 3950.0f; c.thermistorR0 = o["ntc_r0"] | 10000.0f; c.thermistorRFixed = o["ntc_r_fixed"] | 10000.0f; c.thermistorPullup = o["ntc_pullup"] | true;
            c.safeDemand = o["safe_demand"] | 0.0f; c.forceSafeOnFault = o["force_safe_on_fault"] | false; c.minimumRunDemand = o["min_run_demand"] | 0.0f; c.pwmTimingConfigured = !o["pwm_freq_hz"].isNull() || !o["pwm_res_bits"].isNull(); c.pwmFrequency = o["pwm_freq_hz"] | 5000; c.pwmResolution = o["pwm_res_bits"] | 10;
            c.inverted = o["invert"] | false; c.activeHigh = o["active_high"] | true; c.pullup = o["pullup"] | false; c.pulldown = o["pulldown"] | false; c.hasCurrent = o["has_current"] | false; c.currentPin = o["current_pin"] | -1; c.currentMvPerA = o["current_mv_a"] | 100.0f; c.currentZeroV = o["current_zero_v"] | 1.65f; c.currentMaxAmps = o["current_max_a"] | 0.0f;
            if (c.pullup) c.pulldown = false;
            if (!add(c)) return false;
        }
        return true;
    }
};
