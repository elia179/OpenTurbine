#pragma once

// Bounded, allocation-free inventory used by configuration validation and
// runtime binding resolution.  IDs are configuration keys; labels are never
// used to resolve a channel.
#include <Arduino.h>
#include <ArduinoJson.h>

class ChannelRegistry {
public:
    // The registry is the S3-first hardware inventory.  Keep enough room for
    // a complete turbine installation plus auxiliary IO; the legacy ESP32
    // memory profile is deliberately not the sizing constraint for this UI.
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
                      !strcmp(id, "operator_throttle") ||
                      !strcmp(id, "operator_idle") ||
                      !strcmp(id, "battery_voltage") ||
                      !strcmp(id, "batt_voltage_main"));
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
    static bool isDedicatedTemperatureId(const char* id) {
        return id && (!strcmp(id, "tot_main") || !strcmp(id, "tit_main") ||
                      !strcmp(id, "oil_temperature"));
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
               !strcmp(role, "scavenge_pump") ||
               !strcmp(role, "cooling_fan") ||
               !strcmp(role, "valve") ||
               !strcmp(role, "igniter") ||
               !strcmp(role, "ab_igniter") ||
               !strcmp(role, "glow_plug") ||
               !strcmp(role, "fuel_pump") ||
               !strcmp(role, "ab_pump") ||
               !strcmp(role, "prop_pitch") ||
               !strcmp(role, "contactor");
    }
    enum Driver : uint8_t { Digital, Analog, Pulse, RcPwm, Relay, Pwm, Servo };
    struct Channel {
        bool installed = false;
        Direction direction = Input;
        Driver driver = Digital;
        char id[20] = {};
        char name[16] = {};
        char role[18] = {"generic"};
        int8_t pin = -1;
        float minValue = 0.0f, maxValue = 1.0f;
        float pulsesPerUnit = 1.0f; // pulse inputs: speed=pulses/rev, flow=pulses/litre
        float analogZeroMv = 0.0f;      // analog physical roles: mV at zero output
        float analogMvPerUnit = 1000.0f; // speed=RPM, pressure=bar, temp=C, flow=L/min, torque=Nm
        float analogDivider = 1.0f;     // voltage role: Vbatt = ADC volts * divider
        // Temperature cards can be a calibrated analog transmitter (0), a
        // thermocouple amplifier (1=MAX6675, 2=MAX31855, 3=MAX31856), an
        // NTC divider (4), or a DS18B20 OneWire probe (5). SPI bus lines may
        // be shared; each thermocouple amplifier owns its CS pin.
        uint8_t temperatureInterface = 0;
        int8_t spiClk = -1, spiCs = -1, spiMiso = -1, spiMosi = -1;
        char tcType[2] = "K";
        uint8_t temperatureResolution = 12;
        float thermistorBeta = 3950.0f, thermistorR0 = 10000.0f, thermistorRFixed = 10000.0f;
        float safeDemand = 0.0f, faultDemand = 0.0f;
        uint32_t pwmFrequency = 1000;
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
    static bool isCoreManagedOutput(const Channel& c) {
        return isCoreManagedOutputId(c.id);
    }
    struct Binding { char key[20] = {}; char channelId[20] = {}; };

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
        const bool thermocouple = c.direction == Input && !strcmp(c.role, "temperature") &&
                                  c.temperatureInterface >= 1 && c.temperatureInterface <= 3;
        if (!validId(c.id) || findMutable(c.id, Input) || findMutable(c.id, Output) ||
            (!thermocouple && c.pin < 0) || (thermocouple && !temperatureInterfaceValid(c))) return false;
        Channel* list = c.direction == Input ? inputs : outputs;
        uint8_t& count = c.direction == Input ? inputCount : outputCount;
        uint8_t max = c.direction == Input ? MAX_INPUT_CHANNELS : MAX_OUTPUT_CHANNELS;
        if (count >= max || !driverMatches(c.direction, c.driver) || !roleValid(c.direction, c.role) || !semanticDriverValid(c) || !demandsValid(c)) return false;
        for (uint8_t i=0; i<inputCount; ++i) if (c.pin >= 0 && inputs[i].pin == c.pin) return false;
        for (uint8_t i=0; i<outputCount; ++i) if (c.pin >= 0 && outputs[i].pin == c.pin) return false;
        list[count++] = c; return true;
    }
    bool validate() const {
        for (uint8_t i=0; i<inputCount; ++i) if (!validId(inputs[i].id) || !driverMatches(Input, inputs[i].driver) || !roleValid(Input, inputs[i].role) || !semanticDriverValid(inputs[i]) || !temperatureInterfaceValid(inputs[i]) || !demandsValid(inputs[i])) return false;
        for (uint8_t i=0; i<outputCount; ++i) if (!validId(outputs[i].id) || !driverMatches(Output, outputs[i].driver) || !roleValid(Output, outputs[i].role) || !semanticDriverValid(outputs[i]) || !demandsValid(outputs[i])) return false;
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
    static bool validId(const char* id) { if (!id || !id[0] || strlen(id) >= 20) return false; for (;*id;++id) if (!(isalnum(*id)||*id=='_'||*id=='-')) return false; return true; }
private:
    static bool driverMatches(Direction d, Driver v) { return d == Input ? v <= RcPwm : v >= Relay; }
    static bool semanticDriverValid(const Channel& c) {
        if (c.direction != Input) return true;
        if (!strcmp(c.role, "speed") || !strcmp(c.role, "flow"))
            return c.driver == Pulse || c.driver == Analog;
        if (!strcmp(c.role, "pressure") || !strcmp(c.role, "temperature") ||
            !strcmp(c.role, "flame") || !strcmp(c.role, "torque") ||
            !strcmp(c.role, "voltage"))
            return c.driver == Analog;
        if (!strcmp(c.role, "operator")) return c.driver == Analog || c.driver == RcPwm;
        if (!strcmp(c.role, "digital_switch") || !strcmp(c.role, "fault") ||
            !strcmp(c.role, "estop") || !strcmp(c.role, "inhibit_start") ||
            !strcmp(c.role, "sequence_gate") || !strcmp(c.role, "ab_arm") ||
            !strcmp(c.role, "ab_fire") || !strcmp(c.role, "limp_mode"))
            return c.driver == Digital;
        return true;
    }
    static bool temperatureInterfaceValid(const Channel& c) {
        if (!c.temperatureInterface) return true;
        if (c.direction != Input || strcmp(c.role, "temperature")) return false;
        // Special temperature drivers have dedicated TOT, TIT and oil-temp
        // instances. A duplicate/generic card would otherwise stay unhealthy.
        if (!isDedicatedTemperatureId(c.id)) return false;
        if (c.temperatureInterface >= 1 && c.temperatureInterface <= 3) {
            if (c.spiClk < 0 || c.spiCs < 0 || c.spiMiso < 0) return false;
            return c.temperatureInterface != 3 || c.spiMosi >= 0;
        }
        if (c.temperatureInterface == 4)
            return c.pin >= 0 && c.thermistorBeta > 0.0f && c.thermistorR0 > 0.0f && c.thermistorRFixed > 0.0f;
        return c.temperatureInterface == 5 && c.pin >= 0 &&
               c.temperatureResolution >= 9 && c.temperatureResolution <= 12;
    }
    static bool rangeValid(const Channel& c) {
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
        if (c.driver == Pwm) return c.minValue >= 0.0f && c.maxValue <= 1.0f &&
                                    (!c.pwmTimingConfigured || (c.pwmFrequency >= 1 && c.pwmFrequency <= 100000 &&
                                     c.pwmResolution >= 8 && c.pwmResolution <= 14));
        return true;
    }
    static bool demandsValid(const Channel& c) {
        return c.safeDemand >= 0 && c.safeDemand <= 1 &&
               c.faultDemand >= 0 && c.faultDemand <= 1 &&
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
        if (known) return find(b.channelId, expected) != nullptr;
        return find(b.channelId, Input) || find(b.channelId, Output);
    }
    static void write(JsonArray a, const Channel* list, uint8_t n) {
        for (uint8_t i = 0; i < n; i++) {
            const Channel& c = list[i]; JsonObject o = a.add<JsonObject>();
            o["id"] = c.id; o["name"] = c.name; o["role"] = c.role; o["driver"] = (uint8_t)c.driver; o["pin"] = c.pin;
            o["min"] = c.minValue; o["max"] = c.maxValue; o["pulses_per_unit"] = c.pulsesPerUnit;
            o["analog_zero_mv"] = c.analogZeroMv; o["analog_mv_per_unit"] = c.analogMvPerUnit; o["analog_divider"] = c.analogDivider;
            o["temp_interface"] = c.temperatureInterface; o["spi_clk"] = c.spiClk; o["spi_cs"] = c.spiCs; o["spi_miso"] = c.spiMiso; o["spi_mosi"] = c.spiMosi; o["tc_type"] = c.tcType;
            o["temp_resolution"] = c.temperatureResolution; o["ntc_beta"] = c.thermistorBeta; o["ntc_r0"] = c.thermistorR0; o["ntc_r_fixed"] = c.thermistorRFixed;
            o["safe_demand"] = c.safeDemand; o["fault_demand"] = c.faultDemand;
            if (c.pwmTimingConfigured) { o["pwm_freq_hz"] = c.pwmFrequency; o["pwm_res_bits"] = c.pwmResolution; }
            o["invert"] = c.inverted; o["active_high"] = c.activeHigh; o["pullup"] = c.pullup; o["pulldown"] = c.pulldown;
            o["has_current"] = c.hasCurrent; o["current_pin"] = c.currentPin; o["current_mv_a"] = c.currentMvPerA; o["current_zero_v"] = c.currentZeroV; o["current_max_a"] = c.currentMaxAmps;
        }
    }
    bool read(JsonVariantConst v, Direction d) {
        for (JsonObjectConst o : v.as<JsonArrayConst>()) {
            Channel c; c.direction = d; c.installed = true;
            strlcpy(c.id, o["id"] | "", sizeof(c.id)); strlcpy(c.name, o["name"] | c.id, sizeof(c.name)); strlcpy(c.role, o["role"] | "generic", sizeof(c.role));
            c.driver = (Driver)(o["driver"] | 0); c.pin = o["pin"] | -1; c.minValue = o["min"] | 0.0f; c.maxValue = o["max"] | 1.0f;
            c.pulsesPerUnit = o["pulses_per_unit"] | 1.0f; c.analogZeroMv = o["analog_zero_mv"] | 0.0f; c.analogMvPerUnit = o["analog_mv_per_unit"] | 1000.0f; c.analogDivider = o["analog_divider"] | 1.0f;
            c.temperatureInterface = o["temp_interface"] | 0; c.spiClk = o["spi_clk"] | -1; c.spiCs = o["spi_cs"] | -1; c.spiMiso = o["spi_miso"] | -1; c.spiMosi = o["spi_mosi"] | -1; strlcpy(c.tcType, o["tc_type"] | "K", sizeof(c.tcType));
            c.temperatureResolution = o["temp_resolution"] | 12; c.thermistorBeta = o["ntc_beta"] | 3950.0f; c.thermistorR0 = o["ntc_r0"] | 10000.0f; c.thermistorRFixed = o["ntc_r_fixed"] | 10000.0f;
            c.safeDemand = o["safe_demand"] | 0.0f; c.faultDemand = o["fault_demand"] | 0.0f; c.pwmTimingConfigured = !o["pwm_freq_hz"].isNull() || !o["pwm_res_bits"].isNull(); c.pwmFrequency = o["pwm_freq_hz"] | 1000; c.pwmResolution = o["pwm_res_bits"] | 10;
            c.inverted = o["invert"] | false; c.activeHigh = o["active_high"] | true; c.pullup = o["pullup"] | false; c.pulldown = o["pulldown"] | false; c.hasCurrent = o["has_current"] | false; c.currentPin = o["current_pin"] | -1; c.currentMvPerA = o["current_mv_a"] | 100.0f; c.currentZeroV = o["current_zero_v"] | 1.65f; c.currentMaxAmps = o["current_max_a"] | 0.0f;
            if (c.pullup) c.pulldown = false;
            if (!add(c)) return false;
        }
        return true;
    }
};
