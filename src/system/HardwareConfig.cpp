#include "HardwareConfig.h"
#include "Config.h"
#include "hardware_profile.h"
#include "../engine/EngineData.h"
#include <LittleFS.h>
#include <cstring>

namespace {
constexpr int AUTO_S3_RGB_STATUS_LED_PIN = -2;

#if defined(OT_HAS_STATUS_LED) && defined(OT_STATUS_LED_PIN)
constexpr int DEFAULT_STATUS_LED_PIN = OT_STATUS_LED_PIN;
#elif defined(OT_PLATFORM_ESP32S3)
constexpr int DEFAULT_STATUS_LED_PIN = 48;
#else
constexpr int DEFAULT_STATUS_LED_PIN = 2;
#endif

#if defined(OT_PLATFORM_ESP32S3)
constexpr int DEFAULT_STATUS_LED_TYPE = 1;
#else
constexpr int DEFAULT_STATUS_LED_TYPE = 0;
#endif

// ── hardware_profile.h → runtime defaults ────────────────────
// The OT_HAS_* / OT_SAFETY_* macros seed the DEFAULT values below:
//  - first boot with no ecu_config.json generates a file from them,
//  - keys missing from an existing file fall back to them.
// Once saved, ecu_config.json wins — macros never override file values.
#ifdef OT_HAS_N1_RPM
constexpr bool DEFAULT_HAS_N1_RPM = true;
#else
constexpr bool DEFAULT_HAS_N1_RPM = false;
#endif
#ifdef OT_HAS_N2_RPM
constexpr bool DEFAULT_HAS_N2_RPM = true;   // also implies a two-shaft build
#else
constexpr bool DEFAULT_HAS_N2_RPM = false;
#endif
#ifdef OT_HAS_TOT
constexpr bool DEFAULT_HAS_TOT = true;
#else
constexpr bool DEFAULT_HAS_TOT = false;
#endif
#ifdef OT_HAS_OIL_PRESS
constexpr bool DEFAULT_HAS_OIL_PRESS = true;
#else
constexpr bool DEFAULT_HAS_OIL_PRESS = false;
#endif
#ifdef OT_HAS_FLAME
constexpr bool DEFAULT_HAS_FLAME = true;
#else
constexpr bool DEFAULT_HAS_FLAME = false;
#endif
#ifdef OT_HAS_FUEL_FLOW
constexpr bool DEFAULT_HAS_FUEL_FLOW = true;
#else
constexpr bool DEFAULT_HAS_FUEL_FLOW = false;
#endif
#ifdef OT_HAS_P1
constexpr bool DEFAULT_HAS_P1 = true;
#else
constexpr bool DEFAULT_HAS_P1 = false;
#endif
#ifdef OT_HAS_P2
constexpr bool DEFAULT_HAS_P2 = true;
#else
constexpr bool DEFAULT_HAS_P2 = false;
#endif
#ifdef OT_HAS_THROTTLE_INPUT
constexpr bool DEFAULT_HAS_THROTTLE_INPUT = true;
#else
constexpr bool DEFAULT_HAS_THROTTLE_INPUT = false;
#endif
#ifdef OT_THROTTLE_INPUT_RC_PWM
constexpr bool DEFAULT_THROTTLE_INPUT_RC_PWM = true;
#else
constexpr bool DEFAULT_THROTTLE_INPUT_RC_PWM = false;
#endif
#ifdef OT_HAS_IDLE_INPUT
constexpr bool DEFAULT_HAS_IDLE_INPUT = true;
#else
constexpr bool DEFAULT_HAS_IDLE_INPUT = false;
#endif
#ifdef OT_IDLE_INPUT_RC_PWM
constexpr bool DEFAULT_IDLE_INPUT_RC_PWM = true;
#else
constexpr bool DEFAULT_IDLE_INPUT_RC_PWM = false;
#endif
#ifdef OT_HAS_THROTTLE
constexpr bool DEFAULT_HAS_THROTTLE = true;
#else
constexpr bool DEFAULT_HAS_THROTTLE = false;
#endif
#ifdef OT_HAS_STARTER
constexpr bool DEFAULT_HAS_STARTER = true;
#else
constexpr bool DEFAULT_HAS_STARTER = false;
#endif
#ifdef OT_HAS_OIL_PUMP
constexpr bool DEFAULT_HAS_OIL_PUMP = true;
#else
constexpr bool DEFAULT_HAS_OIL_PUMP = false;
#endif
#ifdef OT_HAS_FUEL_SOL
constexpr bool DEFAULT_HAS_FUEL_SOL = true;
#else
constexpr bool DEFAULT_HAS_FUEL_SOL = false;
#endif
#ifdef OT_HAS_IGNITER
constexpr bool DEFAULT_HAS_IGNITER = true;
#else
constexpr bool DEFAULT_HAS_IGNITER = false;
#endif
#ifdef OT_HAS_STARTER_EN
constexpr bool DEFAULT_HAS_STARTER_EN = true;
#else
constexpr bool DEFAULT_HAS_STARTER_EN = false;
#endif
#ifdef OT_HAS_AB_SOL
constexpr bool DEFAULT_HAS_AB_SOL = true;
#else
constexpr bool DEFAULT_HAS_AB_SOL = false;
#endif
#ifdef OT_HAS_AIRSTARTER_SOL
constexpr bool DEFAULT_HAS_AIRSTARTER_SOL = true;
#else
constexpr bool DEFAULT_HAS_AIRSTARTER_SOL = false;
#endif
#ifdef OT_HAS_COOL_FAN
constexpr bool DEFAULT_HAS_COOL_FAN = true;
#else
constexpr bool DEFAULT_HAS_COOL_FAN = false;
#endif
#ifdef OT_HAS_AFTERBURNER
constexpr bool DEFAULT_HAS_AFTERBURNER = true;
#else
constexpr bool DEFAULT_HAS_AFTERBURNER = false;
#endif
#ifdef OT_HAS_CLUSTER_SERIAL
constexpr bool DEFAULT_HAS_CLUSTER_SERIAL = true;
#else
constexpr bool DEFAULT_HAS_CLUSTER_SERIAL = false;
#endif
#ifdef OT_HAS_OIL_LOOP
constexpr bool DEFAULT_HAS_OIL_LOOP = true;
#else
constexpr bool DEFAULT_HAS_OIL_LOOP = false;
#endif
#ifdef OT_HAS_THROTTLE_SLEW
constexpr bool DEFAULT_HAS_THROTTLE_SLEW = true;
#else
constexpr bool DEFAULT_HAS_THROTTLE_SLEW = false;
#endif
#ifdef OT_HAS_DYNAMIC_IDLE
constexpr bool DEFAULT_HAS_DYNAMIC_IDLE = true;
#else
constexpr bool DEFAULT_HAS_DYNAMIC_IDLE = false;
#endif
#ifdef OT_SAFETY_OVERSPEED
constexpr bool DEFAULT_SAFETY_OVERSPEED = true;
#else
constexpr bool DEFAULT_SAFETY_OVERSPEED = false;
#endif
#ifdef OT_SAFETY_OVERTEMP
constexpr bool DEFAULT_SAFETY_OVERTEMP = true;
#else
constexpr bool DEFAULT_SAFETY_OVERTEMP = false;
#endif
#ifdef OT_SAFETY_LOW_OIL
constexpr bool DEFAULT_SAFETY_LOW_OIL = true;
#else
constexpr bool DEFAULT_SAFETY_LOW_OIL = false;
#endif
#ifdef OT_SAFETY_OIL_ZERO
constexpr bool DEFAULT_SAFETY_OIL_ZERO = true;
#else
constexpr bool DEFAULT_SAFETY_OIL_ZERO = false;
#endif
#ifdef OT_SAFETY_FLAMEOUT
constexpr bool DEFAULT_SAFETY_FLAMEOUT = true;
#else
constexpr bool DEFAULT_SAFETY_FLAMEOUT = false;
#endif

// Optional pin/param macros — commented out in the stock profile, so give
// them fallbacks here to keep the defaults below compiling either way.
#ifndef OT_N2_RPM_PIN
  #define OT_N2_RPM_PIN 27
#endif
#ifndef OT_N2_RPM_PPR
  #define OT_N2_RPM_PPR 0.633f
#endif
#ifndef OT_FUEL_FLOW_PIN
  #define OT_FUEL_FLOW_PIN OT_ADC_5
#endif
#ifndef OT_P1_PIN
  #define OT_P1_PIN OT_ADC_5
#endif
#ifndef OT_P2_PIN
  #define OT_P2_PIN OT_ADC_6
#endif
#ifndef OT_AB_SOL_PIN
  #define OT_AB_SOL_PIN -1
#endif
#ifndef OT_AB_SOL_ACTIVE_H
  #define OT_AB_SOL_ACTIVE_H true
#endif
#ifndef OT_AIRSTARTER_SOL_PIN
  #define OT_AIRSTARTER_SOL_PIN -1
#endif
#ifndef OT_COOL_FAN_PIN
  #define OT_COOL_FAN_PIN -1
#endif
#ifndef OT_CLUSTER_TX_PIN
  #define OT_CLUSTER_TX_PIN 17
#endif
#ifndef OT_CLUSTER_BAUD
  #define OT_CLUSTER_BAUD 115200
#endif
#ifndef OT_CLUSTER_INTERVAL_MS
  #define OT_CLUSTER_INTERVAL_MS 50
#endif

// OT_STARTUP_SEQ / OT_SHUTDOWN_SEQ → default sequence block lists.
#define OT_BLOCK(name) #name,
const char* const kProfileStartupSeq[]  = { OT_STARTUP_SEQ };
const char* const kProfileShutdownSeq[] = { OT_SHUTDOWN_SEQ };
#undef OT_BLOCK
constexpr int kProfileStartupSeqLen =
    (int)(sizeof(kProfileStartupSeq) / sizeof(kProfileStartupSeq[0]));
constexpr int kProfileShutdownSeqLen =
    (int)(sizeof(kProfileShutdownSeq) / sizeof(kProfileShutdownSeq[0]));
static_assert(kProfileStartupSeqLen <= HardwareConfig::MAX_SEQ_BLOCKS,
              "OT_STARTUP_SEQ has more blocks than MAX_SEQ_BLOCKS");
static_assert(kProfileShutdownSeqLen <= HardwareConfig::MAX_SEQ_BLOCKS,
              "OT_SHUTDOWN_SEQ has more blocks than MAX_SEQ_BLOCKS");
#ifndef OT_STARTUP_DELAY_MS
  #define OT_STARTUP_DELAY_MS {0}
#endif
#ifndef OT_SHUTDOWN_DELAY_MS
  #define OT_SHUTDOWN_DELAY_MS {0}
#endif
constexpr int kProfileStartupDelayMs[]  = OT_STARTUP_DELAY_MS;
constexpr int kProfileShutdownDelayMs[] = OT_SHUTDOWN_DELAY_MS;
constexpr int kProfileStartupDelayLen =
    (int)(sizeof(kProfileStartupDelayMs) / sizeof(kProfileStartupDelayMs[0]));
constexpr int kProfileShutdownDelayLen =
    (int)(sizeof(kProfileShutdownDelayMs) / sizeof(kProfileShutdownDelayMs[0]));
constexpr int DEFAULT_STATUS_LED_MODE = 0;
constexpr uint32_t DEFAULT_STATUS_LED_STANDBY_COLOR  = 0x00FF40;
constexpr uint32_t DEFAULT_STATUS_LED_STARTUP_COLOR  = 0x0060FF;
constexpr uint32_t DEFAULT_STATUS_LED_RUNNING_COLOR  = 0x00FF00;
constexpr uint32_t DEFAULT_STATUS_LED_SHUTDOWN_COLOR = 0xFF8000;
constexpr uint32_t DEFAULT_STATUS_LED_BLINK_COLOR    = 0x0000FF;

constexpr const char* currentPlatformName() {
#if defined(OT_PLATFORM_ESP32S3)
    return "esp32s3";
#else
    return "esp32";
#endif
}

bool storedHardwarePlatformMismatch(const JsonDocument& doc) {
    const char* stored = doc["platform"] | "";
    return stored[0] && strcmp(stored, currentPlatformName()) != 0;
}

void normalizeS3StatusLedDefault(JsonDocument& doc) {
#if defined(OT_PLATFORM_ESP32S3)
    JsonObject actuators = doc["actuators"].is<JsonObject>()
        ? doc["actuators"].as<JsonObject>()
        : doc["actuators"].to<JsonObject>();
    JsonObject led = actuators["status_led"].is<JsonObject>()
        ? actuators["status_led"].as<JsonObject>()
        : actuators["status_led"].to<JsonObject>();
    const bool enabledPresent = !led["enabled"].isNull();
    const bool pinPresent = !led["pin"].isNull();
    const bool typePresent = !led["type"].isNull();
    const bool enabled = led["enabled"] | true;
    const int pin = led["pin"] | DEFAULT_STATUS_LED_PIN;
    if (!enabledPresent ||
        (enabled && (!pinPresent || pin < 0 || pin == AUTO_S3_RGB_STATUS_LED_PIN || pin == 38))) {
        const int storedMode = led["mode"] | DEFAULT_STATUS_LED_MODE;
        led["enabled"] = true;
        led["pin"] = DEFAULT_STATUS_LED_PIN;
        led["type"] = DEFAULT_STATUS_LED_TYPE;
        led["mode"] = constrain(storedMode, 0, 1);
        if (led["standby_color"].isNull()) led["standby_color"] = DEFAULT_STATUS_LED_STANDBY_COLOR;
        if (led["startup_color"].isNull()) led["startup_color"] = DEFAULT_STATUS_LED_STARTUP_COLOR;
        if (led["running_color"].isNull()) led["running_color"] = DEFAULT_STATUS_LED_RUNNING_COLOR;
        if (led["shutdown_color"].isNull()) led["shutdown_color"] = DEFAULT_STATUS_LED_SHUTDOWN_COLOR;
        if (led["blink_color"].isNull()) led["blink_color"] = DEFAULT_STATUS_LED_BLINK_COLOR;
        JsonVariant sensorsVar = doc["sensors"];
        if (sensorsVar.is<JsonObject>()) {
            JsonObject sensors = sensorsVar.as<JsonObject>();
            const char* spiKeys[] = { "tot", "tit", "oil_temp" };
            for (const char* key : spiKeys) {
                JsonVariant sensorVar = sensors[key];
                if (!sensorVar.is<JsonObject>()) continue;
                JsonObject sensor = sensorVar.as<JsonObject>();
                if ((sensor["miso"] | -1) == 38) sensor["miso"] = OT_SPI_MISO_DEFAULT;
            }
        }
    } else if (enabled && pin == DEFAULT_STATUS_LED_PIN && !typePresent) {
        led["type"] = DEFAULT_STATUS_LED_TYPE;
    }
    if ((led["mode"] | DEFAULT_STATUS_LED_MODE) < 0 ||
        (led["mode"] | DEFAULT_STATUS_LED_MODE) > 1) {
        led["mode"] = DEFAULT_STATUS_LED_MODE;
    }
    if ((led["mode"] | DEFAULT_STATUS_LED_MODE) == 1) {
        led["enabled"] = true;
        led["type"] = 1;
        if ((led["pin"] | -1) < 0 || (led["pin"] | -1) == 38) led["pin"] = DEFAULT_STATUS_LED_PIN;
    }
    if (led["standby_color"].isNull()) led["standby_color"] = DEFAULT_STATUS_LED_STANDBY_COLOR;
    if (led["startup_color"].isNull()) led["startup_color"] = DEFAULT_STATUS_LED_STARTUP_COLOR;
    if (led["running_color"].isNull()) led["running_color"] = DEFAULT_STATUS_LED_RUNNING_COLOR;
    if (led["shutdown_color"].isNull()) led["shutdown_color"] = DEFAULT_STATUS_LED_SHUTDOWN_COLOR;
    if (led["blink_color"].isNull()) led["blink_color"] = DEFAULT_STATUS_LED_BLINK_COLOR;
#else
    (void)doc;
#endif
}

bool gpioAllowed(int pin) {
    if (pin < 0) return true;
#if defined(OT_PLATFORM_ESP32S3)
    return pin <= 48 && pin != 19 && pin != 20 && !(pin >= 22 && pin <= 32);
#else
    return pin <= 39 && !(pin >= 6 && pin <= 11);
#endif
}

bool outputGpioAllowed(int pin) {
    if (!gpioAllowed(pin)) return false;
#if defined(OT_PLATFORM_ESP32)
    return pin < 0 || (pin != 34 && pin != 35 && pin != 36 && pin != 39);
#else
    return pin != 46;
#endif
}

bool adcGpioAllowed(int pin) {
    if (pin < 0) return true;
#if defined(OT_PLATFORM_ESP32S3)
    return pin >= 1 && pin <= 10;
#else
    return pin == 32 || pin == 33 || pin == 34 || pin == 35 || pin == 36 || pin == 39;
#endif
}

int jsonPin(JsonVariantConst object, const char* field) {
    return object[field].isNull() ? -1 : object[field].as<int>();
}

bool enabled(JsonVariantConst object) {
    return !object["enabled"].isNull() && object["enabled"].as<bool>();
}

bool registryHasRole(const ChannelRegistry* registry, ChannelRegistry::Direction direction, const char* role) {
    if (!registry) return false;
    const ChannelRegistry::Channel* channels = direction == ChannelRegistry::Input
        ? registry->inputs
        : registry->outputs;
    const uint8_t count = direction == ChannelRegistry::Input
        ? registry->inputCount
        : registry->outputCount;
    for (uint8_t i = 0; i < count; ++i) {
        if (channels[i].installed && strcmp(channels[i].role, role) == 0) return true;
    }
    return false;
}

bool registryHasBinding(const ChannelRegistry* registry, const char* key, ChannelRegistry::Direction direction) {
    if (!registry) return false;
    for (uint8_t i = 0; i < registry->bindingCount; ++i) {
        if (strcmp(registry->bindings[i].key, key) == 0 &&
            registry->find(registry->bindings[i].channelId, direction)) return true;
    }
    return false;
}

bool docSensorEnabled(const JsonDocument& doc, const char* key) {
    return enabled(doc["sensors"][key]);
}

bool docActuatorEnabled(const JsonDocument& doc, const char* key) {
    return enabled(doc["actuators"][key]);
}

bool validateHardwareDependencies(const JsonDocument& doc, const ChannelRegistry* registry) {
    const bool hasN1 = docSensorEnabled(doc, "n1_rpm") ||
                       registryHasBinding(registry, "primary_n1", ChannelRegistry::Input) ||
                       registryHasRole(registry, ChannelRegistry::Input, "speed");
    const bool hasN2 = (doc["has_two_shaft"] | false) &&
                       (docSensorEnabled(doc, "n2_rpm") ||
                        registryHasBinding(registry, "primary_n2", ChannelRegistry::Input) ||
                        registryHasRole(registry, ChannelRegistry::Input, "speed"));
    const bool hasEgt = docSensorEnabled(doc, "tot") || docSensorEnabled(doc, "tit") ||
                        registryHasBinding(registry, "primary_egt", ChannelRegistry::Input) ||
                        registryHasRole(registry, ChannelRegistry::Input, "temperature");
    const bool hasOilPress = docSensorEnabled(doc, "oil_press") ||
                             registryHasRole(registry, ChannelRegistry::Input, "pressure");
    const bool hasThrottle = docActuatorEnabled(doc, "throttle") ||
                             registryHasRole(registry, ChannelRegistry::Output, "fuel");
    const bool hasOilPump = docActuatorEnabled(doc, "oil_pump") ||
                            registryHasRole(registry, ChannelRegistry::Output, "oil_pump");
    const int propPitchType = doc["actuators"]["prop_pitch"]["type"] | 0;
    const bool hasProportionalPropPitch =
        registryHasRole(registry, ChannelRegistry::Output, "prop_pitch") ||
        (docActuatorEnabled(doc, "prop_pitch") && propPitchType != 2);

    JsonVariantConst controllers = doc["controllers"];
    if ((controllers["oil_loop"] | false) && (!hasOilPress || !hasOilPump)) return false;
    if ((controllers["throttle_slew"] | false) && !hasThrottle) return false;
    if ((controllers["dynamic_idle"] | false) && (!hasThrottle || (!hasN1 && !hasN2))) return false;
    if ((controllers["governor"] | false) && (!hasN2 || (!hasThrottle && !hasProportionalPropPitch))) return false;
    if ((doc["actuators"]["starter"]["assist_enabled"] | false) &&
        (!docActuatorEnabled(doc, "starter") || !hasN1)) return false;

    JsonVariantConst safety = doc["safety"];
    if ((safety["overspeed"] | false) && !hasN1) return false;
    if ((safety["surge"] | false) && !hasN1) return false;
    if ((safety["overtemp"] | false) && !hasEgt) return false;
    if ((safety["hot_start"] | false) && !hasEgt) return false;
    if (((safety["low_oil"] | false) || (safety["oil_zero"] | false)) && !hasOilPress) return false;
    if ((safety["tit_overtemp"] | false) &&
        !docSensorEnabled(doc, "tit") &&
        !registryHasRole(registry, ChannelRegistry::Input, "temperature")) return false;
    if ((safety["oil_temp_high"] | false) &&
        !docSensorEnabled(doc, "oil_temp") &&
        !registryHasRole(registry, ChannelRegistry::Input, "oil_temperature")) return false;
    if ((safety["fuel_press_low"] | false) &&
        !docSensorEnabled(doc, "fuel_press") &&
        !registryHasRole(registry, ChannelRegistry::Input, "fuel_pressure")) return false;
    if ((safety["batt_low"] | false) &&
        !docSensorEnabled(doc, "batt_voltage") &&
        !registryHasRole(registry, ChannelRegistry::Input, "voltage")) return false;
    return true;
}

bool validateOilLoops(JsonVariantConst loops, const ChannelRegistry* registry) {
    if (loops.isNull()) return true;
    if (!loops.is<JsonArrayConst>() || !registry) return false;
    if (loops.size() > HardwareConfig::MAX_OIL_LOOPS) return false;
    auto inRange = [](JsonObjectConst o, const char* key, float lo, float hi) {
        JsonVariantConst v = o[key];
        if (v.isNull()) return true;
        if (!v.is<float>() && !v.is<double>() && !v.is<int>() &&
            !v.is<long>() && !v.is<unsigned int>() && !v.is<unsigned long>()) return false;
        float f = v.as<float>();
        return f >= lo && f <= hi;
    };
    char usedIds[HardwareConfig::MAX_OIL_LOOPS][20] = {};
    char usedPumps[HardwareConfig::MAX_OIL_LOOPS][20] = {};
    uint8_t idCount = 0, pumpCount = 0;
    for (JsonObjectConst loop : loops.as<JsonArrayConst>()) {
        const char* id = loop["id"] | "";
        const char* pressure = loop["pressure_input"] | "";
        const char* pump = loop["pump_output"] | "";
        if (!ChannelRegistry::validId(id) || strlen(id) >= sizeof(HardwareConfig::oilLoops[0].id) ||
            !ChannelRegistry::validId(pressure) ||
            !ChannelRegistry::validId(pump)) return false;
        for (uint8_t i = 0; i < idCount; i++) if (!strcmp(usedIds[i], id)) return false;
        strlcpy(usedIds[idCount++], id, sizeof(usedIds[0]));
        const auto* pressureCh = registry->find(pressure, ChannelRegistry::Input);
        const auto* pumpCh = registry->find(pump, ChannelRegistry::Output);
        if (!pressureCh || !pumpCh ||
            strcmp(pressureCh->role, "pressure") != 0 ||
            strcmp(pumpCh->role, "oil_pump") != 0) return false;
        if (!inRange(loop, "target_bar", 0.0f, 20.0f) ||
            !inRange(loop, "deadband_bar", 0.0f, 5.0f) ||
            !inRange(loop, "min_demand", 0.0f, 1.0f) ||
            !inRange(loop, "max_demand", 0.0f, 1.0f)) return false;
        if ((loop["max_demand"] | 1.0f) < (loop["min_demand"] | 0.0f)) return false;
        if (loop["enabled"] | false) {
            for (uint8_t i = 0; i < pumpCount; i++) if (!strcmp(usedPumps[i], pump)) return false;
            strlcpy(usedPumps[pumpCount++], pump, sizeof(usedPumps[0]));
        }
    }
    return true;
}

int customSensorId(const char* key) {
    if (!key) return -1;
    if (strcmp(key, "oil_temp") == 0) return 0;
    if (strcmp(key, "tot") == 0) return 1;
    if (strcmp(key, "n1_rpm") == 0) return 2;
    if (strcmp(key, "oil_press") == 0) return 3;
    if (strcmp(key, "tit") == 0) return 4;
    if (strcmp(key, "batt_voltage") == 0) return 5;
    if (strcmp(key, "n2_rpm") == 0) return 6;
    if (strcmp(key, "di0") == 0) return 7;
    if (strcmp(key, "di1") == 0) return 8;
    if (strcmp(key, "di2") == 0) return 9;
    if (strcmp(key, "di3") == 0) return 10;
    if (strcmp(key, "fuel_press") == 0) return 11;
    if (strcmp(key, "fuel_flow") == 0) return 12;
    if (strcmp(key, "p1") == 0) return 13;
    if (strcmp(key, "p2") == 0) return 14;
    if (strcmp(key, "torque") == 0) return 15;
    if (strcmp(key, "flame") == 0) return 16;
    if (strcmp(key, "throttle_in") == 0) return 17;
    if (strcmp(key, "idle_in") == 0) return 18;
    if (strcmp(key, "ab_flame") == 0) return 19;
    if (strcmp(key, "glow_current") == 0) return 20;
    if (strcmp(key, "igniter_current") == 0) return 21;
    if (strcmp(key, "igniter2_current") == 0) return 22;
    if (strcmp(key, "oil_pump_current") == 0) return 23;
    if (strcmp(key, "ab_input") == 0) return 24;
    if (strcmp(key, "start_switch") == 0) return 25;
    if (strcmp(key, "stop_switch") == 0) return 26;
    return -1;
}

const char* customSensorKey(uint8_t sensor) {
    static const char* keys[] = {
        "oil_temp", "tot", "n1_rpm", "oil_press", "tit", "batt_voltage",
        "n2_rpm", "di0", "di1", "di2", "di3", "fuel_press", "fuel_flow",
        "p1", "p2", "torque", "flame", "throttle_in", "idle_in",
        "ab_flame", "glow_current", "igniter_current", "igniter2_current",
        "oil_pump_current", "ab_input", "start_switch", "stop_switch"
    };
    return sensor < (sizeof(keys) / sizeof(keys[0])) ? keys[sensor] : "";
}

int customActuatorId(const char* key) {
    if (!key) return -1;
    if (strcmp(key, "cool_fan") == 0) return 0;
    if (strcmp(key, "bleed_valve") == 0) return 1;
    if (strcmp(key, "fuel_pump2") == 0) return 2;
    if (strcmp(key, "oil_scavenge_pump") == 0) return 3;
    if (strcmp(key, "throttle") == 0) return 4;
    if (strcmp(key, "starter") == 0) return 5;
    if (strcmp(key, "starter_en") == 0) return 6;
    if (strcmp(key, "oil_pump") == 0) return 7;
    if (strcmp(key, "fuel_sol") == 0) return 8;
    if (strcmp(key, "igniter") == 0) return 9;
    if (strcmp(key, "igniter2") == 0) return 10;
    if (strcmp(key, "ab_sol") == 0) return 11;
    if (strcmp(key, "ab_pump") == 0) return 12;
    if (strcmp(key, "airstarter_sol") == 0) return 15;
    if (strcmp(key, "glow_plug") == 0) return 16;
    if (strcmp(key, "prop_pitch") == 0) return 17;
    return -1;
}

const char* customActuatorKey(uint8_t act) {
    switch (act) {
        case 0: return "cool_fan";
        case 1: return "bleed_valve";
        case 2: return "fuel_pump2";
        case 3: return "oil_scavenge_pump";
        case 4: return "throttle";
        case 5: return "starter";
        case 6: return "starter_en";
        case 7: return "oil_pump";
        case 8: return "fuel_sol";
        case 9: return "igniter";
        case 10: return "igniter2";
        case 11: return "ab_sol";
        case 12: return "ab_pump";
        case 15: return "airstarter_sol";
        case 16: return "glow_plug";
        case 17: return "prop_pitch";
        default: return "";
    }
}

const char* sequenceSourceId(uint8_t sensor) {
    switch (sensor) {
        case 0:  return "oil_temp_main";
        case 1:  return "tot_main";
        case 2:  return "n1_main";
        case 3:  return "oil_pressure_main";
        case 4:  return "tit_main";
        case 5:  return "batt_voltage_main";
        case 6:  return "n2_main";
        case 7:  return "di0";
        case 8:  return "di1";
        case 9:  return "di2";
        case 10: return "di3";
        case 11: return "fuel_pressure_main";
        case 12: return "fuel_flow_main";
        case 13: return "p1_main";
        case 14: return "p2_main";
        case 15: return "torque_main";
        case 16: return "flame_main";
        case 17: return "throttle_input_main";
        case 18: return "idle_input_main";
        case 19: return "ab_flame_main";
        case 20: return "glow_current_main";
        case 21: return "igniter_current_main";
        case 22: return "igniter2_current_main";
        case 23: return "oil_pump_current_main";
        case 24: return "ab_input_main";
        case 25: return "start_switch";
        case 26: return "stop_switch";
        default: return "";
    }
}

int8_t sequenceSourceHandle(const char* id) {
    if (!id || !id[0]) return -1;
    for (uint8_t i = 0; i <= 26; ++i) {
        if (strcmp(id, sequenceSourceId(i)) == 0 || strcmp(id, customSensorKey(i)) == 0)
            return (int8_t)i;
    }
    if (strcmp(id, "primary_n1") == 0) return 2;
    if (strcmp(id, "primary_n2") == 0) return 6;
    if (strcmp(id, "primary_egt") == 0) return 1;
    if (strcmp(id, "operator_throttle") == 0) return 17;
    if (const auto* c = HardwareConfig::channelRegistry.find(id, ChannelRegistry::Input)) {
        if (strcmp(c->role, "speed") == 0) return 2;
        if (strcmp(c->role, "pressure") == 0) return 3;
        if (strcmp(c->role, "temperature") == 0) return 1;
        if (strcmp(c->role, "operator") == 0) return 17;
    }
    return -1;
}

const char* sequenceTargetId(uint8_t act) {
    switch (act) {
        case 0:  return "cooling_fan_main";
        case 1:  return "bleed_valve_main";
        case 2:  return "fuel_pump2_main";
        case 3:  return "oil_scavenge_main";
        case 4:  return "main_fuel";
        case 5:  return "starter_main";
        case 6:  return "starter_enable_main";
        case 7:  return "oil_pump_main";
        case 8:  return "fuel_solenoid_main";
        case 9:  return "igniter_main";
        case 10: return "igniter2_main";
        case 11: return "ab_solenoid_main";
        case 12: return "ab_pump_main";
        case 13: return "request_shutdown";
        case 14: return "request_fault";
        case 15: return "airstarter_main";
        case 16: return "glow_plug_main";
        case 17: return "prop_pitch_main";
        default: return "";
    }
}

int8_t sequenceTargetHandle(const char* id) {
    if (!id || !id[0]) return -1;
    for (uint8_t i = 0; i <= 17; ++i) {
        if (strcmp(id, sequenceTargetId(i)) == 0 || strcmp(id, customActuatorKey(i)) == 0)
            return (int8_t)i;
    }
    if (strcmp(id, "main_fuel_output") == 0) return 4;
    if (strcmp(id, "main_starter") == 0) return 5;
    if (strcmp(id, "main_fuel_shutoff") == 0) return 8;
    if (const auto* c = HardwareConfig::channelRegistry.find(id, ChannelRegistry::Output)) {
        if (strcmp(c->role, "fuel") == 0) return 4;
        if (strcmp(c->role, "starter") == 0) return 5;
        if (strcmp(c->role, "oil_pump") == 0) return 7;
        if (strcmp(c->role, "cooling_fan") == 0) return 0;
        if (strcmp(c->role, "valve") == 0) return 1;
        if (strcmp(c->role, "scavenge_pump") == 0) return 3;
    }
    return -1;
}

bool customActuatorIsAnalog(uint8_t act) {
    switch (act) {
        case 2:  return HardwareConfig::fuelPump2Type != 2;
        case 4:  return HardwareConfig::throttleType != 2;
        case 5:  return HardwareConfig::starterType != 2;
        case 7:  return HardwareConfig::oilPumpType != 2;
        case 12: return HardwareConfig::abPumpType != 2;
        case 16: return HardwareConfig::glowPlugOutputType != 1;
        case 17: return HardwareConfig::propPitchType != 2;
        default: return false;
    }
}

float customThresholdToStored(uint8_t sensor, float value) {
    return (sensor == 17 || sensor == 18 || sensor == 24) ? constrain(value, 0.0f, 100.0f) / 100.0f : value;
}

float customThresholdToDisplay(uint8_t sensor, float value) {
    return (sensor == 17 || sensor == 18 || sensor == 24) ? value * 100.0f : value;
}

float customActuatorValueToStored(uint8_t actuator, float value) {
    if (customActuatorIsAnalog(actuator)) return constrain(value, 0.0f, 100.0f) / 100.0f;
    return value >= 0.5f ? 1.0f : 0.0f;
}

float customActuatorValueToDisplay(uint8_t actuator, float value) {
    if (customActuatorIsAnalog(actuator)) return value * 100.0f;
    return value >= 0.5f ? 1.0f : 0.0f;
}

uint8_t customOpId(const char* op) {
    if (!op) return 0;
    if (strcmp(op, "<") == 0) return 1;
    if (strcmp(op, ">=") == 0) return 2;
    if (strcmp(op, "<=") == 0) return 3;
    if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0) return 4;
    return 0;
}

const char* customOpString(uint8_t op) {
    switch (op) {
        case 1: return "<";
        case 2: return ">=";
        case 3: return "<=";
        case 4: return "==";
        default: return ">";
    }
}

void clearSeqSideActions(
    HardwareConfig::SeqSideAction actions[HardwareConfig::MAX_SEQ_BLOCKS][HardwareConfig::MAX_SEQ_SIDE_ACTIONS]) {
    memset(actions, 0, sizeof(HardwareConfig::SeqSideAction) *
                       HardwareConfig::MAX_SEQ_BLOCKS *
                       HardwareConfig::MAX_SEQ_SIDE_ACTIONS);
}

void writeSeqSideActions(
    JsonDocument& doc, const char* key, int seqLen,
    HardwareConfig::SeqSideAction actions[HardwareConfig::MAX_SEQ_BLOCKS][HardwareConfig::MAX_SEQ_SIDE_ACTIONS]) {
    JsonArray outer = doc[key].to<JsonArray>();
    for (int i = 0; i < seqLen; i++) {
        JsonArray slot = outer.add<JsonArray>();
        for (int j = 0; j < HardwareConfig::MAX_SEQ_SIDE_ACTIONS; j++) {
            const auto& a = actions[i][j];
            if (!a.enabled) continue;
            JsonObject item = slot.add<JsonObject>();
            item["act"] = a.actuator;
            item["target"] = sequenceTargetId(a.actuator);
            item["value"] = a.value;
        }
    }
}

void readSeqSideActions(
    const JsonDocument& doc, const char* key, int seqLen,
    HardwareConfig::SeqSideAction actions[HardwareConfig::MAX_SEQ_BLOCKS][HardwareConfig::MAX_SEQ_SIDE_ACTIONS]) {
    clearSeqSideActions(actions);
    if (!doc[key].is<JsonArrayConst>()) return;
    JsonArrayConst outer = doc[key];
    for (int i = 0; i < seqLen && i < (int)outer.size() && i < HardwareConfig::MAX_SEQ_BLOCKS; i++) {
        if (!outer[i].is<JsonArrayConst>()) continue;
        JsonArrayConst slot = outer[i];
        int out = 0;
        for (JsonObjectConst item : slot) {
            if (out >= HardwareConfig::MAX_SEQ_SIDE_ACTIONS) break;
            const char* target = item["target"] | "";
            int act = target[0] ? sequenceTargetHandle(target) : (item["act"] | -1);
            if (act < 0 || act > 17) continue;
            actions[i][out].enabled = true;
            actions[i][out].actuator = (uint8_t)act;
            actions[i][out].value = constrain(item["value"] | 0.0f, 0.0f, 1.0f);
            out++;
        }
    }
}

void clearCustomBlocks() {
    memset(HardwareConfig::customBlocks, 0,
           sizeof(HardwareConfig::CustomBlockDef) * HardwareConfig::MAX_CUSTOM_BLOCKS);
    HardwareConfig::customBlockCount = 0;
}

void writeCustomBlocks(JsonDocument& doc) {
    JsonObject root = doc["custom_blocks"].to<JsonObject>();
    for (int i = 0; i < HardwareConfig::customBlockCount; i++) {
        const auto& def = HardwareConfig::customBlocks[i];
        if (!def.enabled || !def.key[0]) continue;
        JsonObject item = root[def.key].to<JsonObject>();
        item["label"] = def.label;
        item["desc"] = def.desc;
        item["type"] = def.type == 1 ? "wait" : (def.type == 2 ? "while" : "action");
        JsonArray steps = item["steps"].to<JsonArray>();
        for (uint8_t s = 0; s < def.stepCount; s++) {
            const auto& step = def.steps[s];
            JsonObject so = steps.add<JsonObject>();
            if (step.type == 1) {
                so["type"] = "delay_ms";
                so["val"] = step.delayMs;
            } else {
                so["type"] = "set_act";
                so["act"] = customActuatorKey(step.actuator);
                so["target"] = sequenceTargetId(step.actuator);
                so["val"] = customActuatorValueToDisplay(step.actuator, step.value);
            }
        }
        if (def.type == 1) {
            item["duration_ms"] = def.durationMs;
        } else if (def.type == 2) {
            JsonObject cond = item["condition"].to<JsonObject>();
            cond["sensor"] = customSensorKey(def.sensor);
            cond["source"] = sequenceSourceId(def.sensor);
            cond["op"] = customOpString(def.op);
            cond["value"] = customThresholdToDisplay(def.sensor, def.threshold);
            item["timeout_ms"] = def.timeoutMs;
            item["timeout_action"] = def.timeoutAction == 1 ? "fault" :
                                     (def.timeoutAction == 2 ? "continue" : "abort");
        }
    }
}

void readCustomBlocks(const JsonDocument& doc) {
    clearCustomBlocks();
    if (!doc["custom_blocks"].is<JsonObjectConst>()) return;
    JsonObjectConst root = doc["custom_blocks"];
    for (JsonPairConst kv : root) {
        if (HardwareConfig::customBlockCount >= HardwareConfig::MAX_CUSTOM_BLOCKS) break;
        const char* key = kv.key().c_str();
        if (!key || strncmp(key, "custom_", 7) != 0) continue;
        JsonObjectConst item = kv.value().as<JsonObjectConst>();
        if (item.isNull()) continue;

        HardwareConfig::CustomBlockDef def{};
        def.enabled = true;
        strncpy(def.key, key, sizeof(def.key) - 1);
        def.key[sizeof(def.key) - 1] = '\0';
        strncpy(def.label, item["label"] | key, sizeof(def.label) - 1);
        def.label[sizeof(def.label) - 1] = '\0';
        strncpy(def.desc, item["desc"] | "", sizeof(def.desc) - 1);
        def.desc[sizeof(def.desc) - 1] = '\0';
        const char* type = item["type"] | "action";
        def.type = strcmp(type, "wait") == 0 ? 1 : (strcmp(type, "while") == 0 ? 2 : 0);
        def.durationMs = constrain((uint32_t)(item["duration_ms"] | 1000UL), 100UL, 600000UL);
        def.timeoutMs = constrain((uint32_t)(item["timeout_ms"] | 10000UL), 0UL, 600000UL);
        const char* timeoutAction = item["timeout_action"] | "abort";
        def.timeoutAction = strcmp(timeoutAction, "fault") == 0 ? 1 :
                            (strcmp(timeoutAction, "continue") == 0 ? 2 : 0);

        if (def.type == 2 && item["condition"].is<JsonObjectConst>()) {
            JsonObjectConst cond = item["condition"];
            const char* source = cond["source"] | "";
            int sensor = source[0] ? sequenceSourceHandle(source) : customSensorId(cond["sensor"] | "");
            if (sensor < 0) continue;
            def.sensor = (uint8_t)sensor;
            def.op = customOpId(cond["op"] | ">");
            def.threshold = customThresholdToStored(def.sensor, cond["value"] | 0.0f);
        }

        if (item["steps"].is<JsonArrayConst>()) {
            JsonArrayConst steps = item["steps"];
            for (JsonObjectConst step : steps) {
                if (def.stepCount >= HardwareConfig::MAX_CUSTOM_STEPS) break;
                const char* st = step["type"] | "";
                auto& out = def.steps[def.stepCount];
                if (strcmp(st, "delay_ms") == 0) {
                    out.type = 1;
                    out.delayMs = constrain((uint32_t)(step["val"] | 0UL), 0UL, 600000UL);
                    def.stepCount++;
                } else if (strcmp(st, "set_act") == 0) {
                    const char* target = step["target"] | "";
                    int act = target[0] ? sequenceTargetHandle(target) : customActuatorId(step["act"] | "");
                    if (act < 0) continue;
                    out.type = 0;
                    out.actuator = (uint8_t)act;
                    out.value = customActuatorValueToStored(out.actuator, step["val"] | 0.0f);
                    def.stepCount++;
                }
            }
        }

        if (def.type == 0 && def.stepCount == 0) continue;
        HardwareConfig::customBlocks[HardwareConfig::customBlockCount++] = def;
    }
}

bool seqActionActuatorAvailable(uint8_t act) {
    switch (act) {
        case 0:  return HardwareConfig::hasCoolFan;
        case 1:  return HardwareConfig::hasBleedValve;
        case 2:  return HardwareConfig::hasFuelPump2;
        case 3:  return HardwareConfig::hasOilScavengePump;
        case 4:  return HardwareConfig::hasThrottle;
        case 5:  return HardwareConfig::hasStarter;
        case 6:  return HardwareConfig::hasStarterEn;
        case 7:  return HardwareConfig::hasOilPump;
        case 8:  return HardwareConfig::hasFuelSol;
        case 9:  return HardwareConfig::hasIgniter;
        case 10: return HardwareConfig::hasIgniter2;
        case 11: return HardwareConfig::hasAfterburner && HardwareConfig::hasAbSol;
        case 12: return HardwareConfig::hasAfterburner && HardwareConfig::hasAbPump;
        case 15: return HardwareConfig::hasAirstarterSol;
        case 16: return HardwareConfig::hasGlowPlug;
        case 17: return HardwareConfig::hasPropPitch;
        default: return false;
    }
}

bool ruleSensorAvailable(uint8_t sensor) {
    switch (sensor) {
        case 0:  return HardwareConfig::hasOilTemp;
        case 1:  return HardwareConfig::hasTot;
        case 2:  return HardwareConfig::hasN1Rpm;
        case 3:  return HardwareConfig::hasOilPress;
        case 4:  return HardwareConfig::hasTit;
        case 5:  return HardwareConfig::hasBattVoltage;
        case 6:  return HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm;
        case 7:  return HardwareConfig::diCh[0].pin >= 0;
        case 8:  return HardwareConfig::diCh[1].pin >= 0;
        case 9:  return HardwareConfig::diCh[2].pin >= 0;
        case 10: return HardwareConfig::diCh[3].pin >= 0;
        case 11: return HardwareConfig::hasFuelPress;
        case 12: return HardwareConfig::hasFuelFlow;
        case 13: return HardwareConfig::hasP1;
        case 14: return HardwareConfig::hasP2;
        case 15: return HardwareConfig::hasTorque;
        case 16: return HardwareConfig::hasFlame;
        case 17: return HardwareConfig::hasThrottleInput;
        case 18: return HardwareConfig::hasIdleInput;
        case 19: return HardwareConfig::hasAfterburner && HardwareConfig::hasAbFlame;
        case 20: return HardwareConfig::hasGlowPlug && HardwareConfig::hasGlowCurrentSensor;
        case 21: return HardwareConfig::hasIgniter && HardwareConfig::hasIgniterCurrentSensor;
        case 22: return HardwareConfig::hasIgniter2 && HardwareConfig::hasIgniter2CurrentSensor;
        case 23: return HardwareConfig::hasOilPump && HardwareConfig::hasOilPumpCurrentSensor;
        case 24: return HardwareConfig::hasAfterburner && HardwareConfig::abInputPin >= 0;
        case 25: return HardwareConfig::startPin >= 0;
        case 26: return HardwareConfig::stopPin >= 0;
        default: return false;
    }
}

int customBlockIndexByKey(const char* key) {
    if (!key || !key[0]) return -1;
    for (int i = 0; i < HardwareConfig::customBlockCount; i++) {
        if (HardwareConfig::customBlocks[i].enabled &&
            strcmp(HardwareConfig::customBlocks[i].key, key) == 0) return i;
    }
    return -1;
}

bool customBlockAvailable(const char* key) {
    int idx = customBlockIndexByKey(key);
    if (idx < 0) return false;
    const auto& def = HardwareConfig::customBlocks[idx];
    if (def.type > 2) return false;
    if (def.type == 2 && !ruleSensorAvailable(def.sensor)) return false;
    if (def.type == 0 && def.stepCount == 0) return false;
    for (uint8_t i = 0; i < def.stepCount; i++) {
        const auto& step = def.steps[i];
        if (step.type == 0 && !seqActionActuatorAvailable(step.actuator)) return false;
    }
    return true;
}

void sanitizeSeqSideActions(
    HardwareConfig::SeqSideAction actions[HardwareConfig::MAX_SEQ_BLOCKS][HardwareConfig::MAX_SEQ_SIDE_ACTIONS]) {
    for (int i = 0; i < HardwareConfig::MAX_SEQ_BLOCKS; i++) {
        int out = 0;
        for (int j = 0; j < HardwareConfig::MAX_SEQ_SIDE_ACTIONS; j++) {
            auto a = actions[i][j];
            if (!a.enabled || !seqActionActuatorAvailable(a.actuator)) continue;
            a.value = constrain(a.value, 0.0f, 1.0f);
            actions[i][out++] = a;
        }
        for (; out < HardwareConfig::MAX_SEQ_SIDE_ACTIONS; out++) {
            actions[i][out] = HardwareConfig::SeqSideAction{};
        }
    }
}

bool sequenceBlockAvailable(const char* name) {
    if (!name || !name[0]) return false;
    if (strncmp(name, "custom_", 7) == 0) return customBlockAvailable(name);
    if (strcmp(name, "OilPrime") == 0 || strcmp(name, "OilPumpOn") == 0 || strcmp(name, "OilPumpOff") == 0)
        return HardwareConfig::hasOilPump;
    if (strcmp(name, "StarterSpin") == 0 || strcmp(name, "StarterOff") == 0)
        return HardwareConfig::hasStarter;
    if (strcmp(name, "FuelPumpIdle") == 0 || strcmp(name, "ModifiedIdle") == 0 ||
        strcmp(name, "Spool") == 0 || strcmp(name, "ThrottleSet") == 0)
        return HardwareConfig::hasThrottle;
    if (strcmp(name, "FuelOpen") == 0 || strcmp(name, "FuelSolClose") == 0 || strcmp(name, "FuelPulse") == 0)
        return HardwareConfig::hasFuelSol;
    if (strcmp(name, "PreIgnSpark") == 0)
        return HardwareConfig::hasIgniter;
    if (strcmp(name, "PreHeat") == 0 ||
        strcmp(name, "IgniterOn") == 0 || strcmp(name, "IgniterOff") == 0)
        return HardwareConfig::hasIgniter || HardwareConfig::hasIgniter2 || HardwareConfig::hasGlowPlug;
    if (strcmp(name, "FlameConfirm") == 0) return HardwareConfig::hasFlame;
    if (strcmp(name, "TempConfirm") == 0 || strcmp(name, "WaitTOTCool") == 0)
        return HardwareConfig::hasTot || HardwareConfig::hasTit;
    if (strcmp(name, "StarterEnOn") == 0 || strcmp(name, "StarterEnOff") == 0) return HardwareConfig::hasStarterEn;
    if (strcmp(name, "OilScavengeOn") == 0 || strcmp(name, "OilScavengeOff") == 0) return HardwareConfig::hasOilScavengePump;
    if (strcmp(name, "AirstarterOn") == 0 || strcmp(name, "AirstarterOff") == 0) return HardwareConfig::hasAirstarterSol;
    if (strcmp(name, "CoolFanOn") == 0 || strcmp(name, "CoolFanOff") == 0) return HardwareConfig::hasCoolFan;
    if (strcmp(name, "BleedOpen") == 0 || strcmp(name, "BleedClose") == 0) return HardwareConfig::hasBleedValve;
    if (strcmp(name, "GlowPreheat") == 0) return HardwareConfig::hasGlowPlug;
    if (strcmp(name, "FuelPumpRamp") == 0 || strcmp(name, "FuelPump2Set") == 0 ||
        strcmp(name, "FuelPump2On") == 0 || strcmp(name, "FuelPump2Off") == 0) return HardwareConfig::hasFuelPump2;
    if (strcmp(name, "GovernorHold") == 0)
        return HardwareConfig::hasGovernor && HardwareConfig::hasN2Rpm &&
               (HardwareConfig::hasThrottle || HardwareConfig::hasPropPitch);
    if (strncmp(name, "AB", 2) == 0 || strcmp(name, "ABSolOpen") == 0 || strcmp(name, "ABSolClose") == 0)
        return HardwareConfig::hasAfterburner;
    return true;
}

void sanitizeSequenceBlocks(
    char seq[HardwareConfig::MAX_SEQ_BLOCKS][24], int& len, int delays[HardwareConfig::MAX_SEQ_BLOCKS],
    uint8_t ignitionTargets[HardwareConfig::MAX_SEQ_BLOCKS],
    HardwareConfig::SeqSideAction enterActions[HardwareConfig::MAX_SEQ_BLOCKS][HardwareConfig::MAX_SEQ_SIDE_ACTIONS],
    HardwareConfig::SeqSideAction exitActions[HardwareConfig::MAX_SEQ_BLOCKS][HardwareConfig::MAX_SEQ_SIDE_ACTIONS]) {
    int out = 0;
    for (int i = 0; i < len; i++) {
        if (!sequenceBlockAvailable(seq[i])) continue;
        if (out != i) {
            strncpy(seq[out], seq[i], sizeof(seq[out]) - 1);
            seq[out][sizeof(seq[out]) - 1] = '\0';
            delays[out] = delays[i];
            ignitionTargets[out] = constrain(ignitionTargets[i], 0, 2);
            memcpy(enterActions[out], enterActions[i], sizeof(enterActions[out]));
            memcpy(exitActions[out], exitActions[i], sizeof(exitActions[out]));
        }
        out++;
    }
    for (int i = out; i < HardwareConfig::MAX_SEQ_BLOCKS; i++) {
        seq[i][0] = '\0';
        delays[i] = 0;
        ignitionTargets[i] = 0;
        memset(enterActions[i], 0, sizeof(enterActions[i]));
        memset(exitActions[i], 0, sizeof(exitActions[i]));
    }
    len = out;
}

bool intRange(JsonVariantConst object, const char* field, long minValue, long maxValue) {
    if (object[field].isNull()) return true;
    if (!object[field].is<int>() && !object[field].is<long>() &&
        !object[field].is<unsigned int>() && !object[field].is<unsigned long>()) return false;
    long value = object[field].as<long>();
    return value >= minValue && value <= maxValue;
}

bool numberRange(JsonVariantConst object, const char* field, float minValue, float maxValue) {
    if (object[field].isNull()) return true;
    if (!object[field].is<float>() && !object[field].is<double>() &&
        !object[field].is<int>() && !object[field].is<long>()) return false;
    float value = object[field].as<float>();
    return value >= minValue && value <= maxValue;
}

bool optionalStringFits(JsonVariantConst value, size_t capacity) {
    if (value.isNull()) return true;
    if (!value.is<const char*>()) return false;
    const char* text = value.as<const char*>();
    return text && strlen(text) < capacity;
}

bool requiredStringFits(JsonVariantConst value, size_t capacity) {
    if (!value.is<const char*>()) return false;
    const char* text = value.as<const char*>();
    return text && text[0] && strlen(text) < capacity;
}

bool validateDisplayLabels(JsonVariantConst labels) {
    if (labels.isNull()) return true;
    if (!labels.is<JsonObjectConst>()) return false;
    static constexpr const char* keys[] = {
        "tot", "tit", "n1", "n2", "oil_press", "oil_temp", "p1", "p2",
        "fuel_press", "fuel_flow", "stop", "start", "ab_arm"
    };
    for (const char* key : keys) {
        if (!optionalStringFits(labels[key], sizeof(HardwareConfig::labelTot))) return false;
    }
    return true;
}

bool validateCustomBlockStrings(JsonVariantConst blocks) {
    if (blocks.isNull()) return true;
    if (!blocks.is<JsonObjectConst>()) return false;
    for (JsonPairConst kv : blocks.as<JsonObjectConst>()) {
        const char* key = kv.key().c_str();
        if (!key || strncmp(key, "custom_", 7) != 0) continue;
        if (strlen(key) >= sizeof(HardwareConfig::customBlocks[0].key)) return false;
        if (!kv.value().is<JsonObjectConst>()) return false;
        JsonObjectConst item = kv.value().as<JsonObjectConst>();
        if (!optionalStringFits(item["label"], sizeof(HardwareConfig::customBlocks[0].label)) ||
            !optionalStringFits(item["desc"], sizeof(HardwareConfig::customBlocks[0].desc))) return false;
    }
    return true;
}

bool pwmPercentRange(JsonVariantConst object, const char* minField, const char* maxField) {
    if (!numberRange(object, minField, 0.0f, 100.0f) ||
        !numberRange(object, maxField, 0.0f, 100.0f)) return false;
    if (!object[minField].isNull() && !object[maxField].isNull() &&
        object[maxField].as<float>() < object[minField].as<float>()) return false;
    return true;
}

bool requiredPinAllowed(JsonVariantConst object, const char* field, bool (*allowed)(int)) {
    const int pin = jsonPin(object, field);
    return pin >= 0 && allowed(pin);
}

bool optionalPinAllowed(JsonVariantConst object, const char* field, bool (*allowed)(int)) {
    const int pin = jsonPin(object, field);
    return pin < 0 || allowed(pin);
}

bool validatePlatformPins(const JsonDocument& doc) {
    const bool hasAfterburner = doc["has_afterburner"] | false;
    const bool hasTwoShaft = doc["has_two_shaft"] | false;
    JsonVariantConst controls = doc["controls"];
    const int stopPin = jsonPin(controls, "stop_pin");
    const int startPin = jsonPin(controls, "start_pin");
    if (stopPin < 0 || startPin < 0 ||
        !gpioAllowed(stopPin) || !gpioAllowed(startPin) ||
        (stopPin >= 0 && stopPin == startPin)) return false;

    JsonVariantConst sensors = doc["sensors"];
    if (enabled(sensors["n1_rpm"]) &&
        !requiredPinAllowed(sensors["n1_rpm"], "pin", gpioAllowed)) return false;
    if (hasTwoShaft && enabled(sensors["n2_rpm"]) &&
        !requiredPinAllowed(sensors["n2_rpm"], "pin", gpioAllowed)) return false;

    const char* analogSensors[] = { "oil_press", "flame", "fuel_press", "p1", "p2", "batt_voltage" };
    for (const char* key : analogSensors)
        if (enabled(sensors[key]) && !requiredPinAllowed(sensors[key], "pin", adcGpioAllowed)) return false;
    if (!numberRange(sensors["batt_voltage"], "divider", 1.0f, 100.0f)) return false;

    JsonVariantConst fuelFlow = sensors["fuel_flow"];
    if (enabled(fuelFlow) &&
        !((fuelFlow["type"] | 0) ? requiredPinAllowed(fuelFlow, "pin", gpioAllowed)
                                 : requiredPinAllowed(fuelFlow, "pin", adcGpioAllowed))) return false;

    const char* inputSensors[] = { "throttle_input", "idle_input" };
    for (const char* key : inputSensors) {
        JsonVariantConst item = sensors[key];
        if (enabled(item) &&
            !((item["rc_pwm"] | false) ? requiredPinAllowed(item, "pin", gpioAllowed)
                                       : requiredPinAllowed(item, "pin", adcGpioAllowed))) return false;
    }

    auto validTcChip = [](const char* chip) {
        return strcmp(chip, "max6675") == 0 ||
               strcmp(chip, "max31855") == 0 ||
               strcmp(chip, "max31856") == 0;
    };
    const char* spiSensors[] = { "tot", "tit" };
    for (const char* key : spiSensors) {
        JsonVariantConst item = sensors[key];
        const char* chip = item["chip"] | "max6675";
        if (enabled(item) &&
            (!validTcChip(chip) ||
             !requiredPinAllowed(item, "clk", outputGpioAllowed) ||
             !requiredPinAllowed(item, "cs", outputGpioAllowed) ||
             !requiredPinAllowed(item, "miso", gpioAllowed))) return false;
        // MAX31856 needs MOSI: the driver writes CR0/CR1 and readback-verifies;
        // without MOSI configuration fails and the sensor is permanently
        // unhealthy — reject rather than let the save look complete.
        if (enabled(item) && strcmp(chip, "max31856") == 0) {
            if (!requiredPinAllowed(item, "mosi", outputGpioAllowed)) return false;
        } else if (enabled(item)) {
            if (!optionalPinAllowed(item, "mosi", outputGpioAllowed)) return false;
        }
    }

    JsonVariantConst oilTemp = sensors["oil_temp"];
    if (enabled(oilTemp)) {
        const char* chip = oilTemp["chip"] | "ntc";
        const bool validOilTempChip = strcmp(chip, "ntc") == 0 ||
                                      strcmp(chip, "ds18b20") == 0 ||
                                      validTcChip(chip);
        if (!validOilTempChip) return false;
        if (strcmp(chip, "ntc") == 0 &&
            (!requiredPinAllowed(oilTemp, "pin", adcGpioAllowed) ||
             !numberRange(oilTemp, "ntc_beta", 1000.0f, 10000.0f) ||
             !numberRange(oilTemp, "ntc_r0", 100.0f, 1000000.0f) ||
             !numberRange(oilTemp, "ntc_r_fixed", 100.0f, 1000000.0f))) return false;
        if (strcmp(chip, "ntc") == 0 && (oilTemp["use_raw_poly"] | false)) {
            if (!numberRange(oilTemp, "poly_a", -1000000.0f, 1000000.0f) ||
                !numberRange(oilTemp, "poly_b", -1000000.0f, 1000000.0f) ||
                !numberRange(oilTemp, "poly_c", -1000000.0f, 1000000.0f) ||
                !numberRange(oilTemp, "poly_d", -1000000.0f, 1000000.0f) ||
                !numberRange(oilTemp, "poly_x_min", 0.0f, 4095.0f) ||
                !numberRange(oilTemp, "poly_x_max", 0.0f, 4095.0f) ||
                (oilTemp["poly_x_max"] | 0.0f) <= (oilTemp["poly_x_min"] | 0.0f)) return false;
        }
        if (strcmp(chip, "ds18b20") == 0 && !requiredPinAllowed(oilTemp, "pin", gpioAllowed)) return false;
        if (strcmp(chip, "ntc") != 0 && strcmp(chip, "ds18b20") != 0 &&
            (!requiredPinAllowed(oilTemp, "clk", outputGpioAllowed) ||
             !requiredPinAllowed(oilTemp, "cs", outputGpioAllowed) ||
             !requiredPinAllowed(oilTemp, "miso", gpioAllowed))) return false;
        // Same MAX31856 MOSI requirement as TOT/TIT above.
        if (strcmp(chip, "max31856") == 0) {
            if (!requiredPinAllowed(oilTemp, "mosi", outputGpioAllowed)) return false;
        } else if (strcmp(chip, "ntc") != 0 && strcmp(chip, "ds18b20") != 0) {
            if (!optionalPinAllowed(oilTemp, "mosi", outputGpioAllowed)) return false;
        }
    }

    JsonVariantConst torque = sensors["torque"];
    if (enabled(torque)) {
        if (torque["hx711"] | false) {
            if (!requiredPinAllowed(torque, "dt_pin", gpioAllowed) ||
                !requiredPinAllowed(torque, "clk_pin", outputGpioAllowed)) return false;
        } else if (!requiredPinAllowed(torque, "pin", adcGpioAllowed)) return false;
    }
    if (!numberRange(torque, "scale", 0.001f, 100000.0f) ||
        !numberRange(torque, "offset", -100000.0f, 100000.0f) ||
        !numberRange(torque, "hx_scale", 0.000001f, 1000000.0f)) return false;

    JsonVariantConst actuators = doc["actuators"];
    const char* actuatorNames[] = {
        "throttle", "starter", "oil_pump", "fuel_sol", "igniter", "igniter2",
        "starter_en", "ab_sol", "airstarter_sol", "cool_fan", "ab_pump",
        "oil_scavenge_pump", "fuel_pump2", "bleed_valve", "prop_pitch",
        "glow_plug", "status_led"
    };
    for (const char* key : actuatorNames) {
        JsonVariantConst item = actuators[key];
        if (!hasAfterburner &&
            (strcmp(key, "ab_sol") == 0 || strcmp(key, "ab_pump") == 0)) continue;
        if (enabled(item)) {
            const int pin = jsonPin(item, "pin");
            if (strcmp(key, "status_led") == 0) {
                const int ledType = item["type"] | 0;
                const int ledMode = item["mode"] | 0;
                if (ledType < 0 || ledType > 1) return false;
                if (ledMode < 0 || ledMode > 1) return false;
                if (pin == AUTO_S3_RGB_STATUS_LED_PIN) {
#if defined(OT_PLATFORM_ESP32S3)
                    continue;
#else
                    return false;
#endif
                }
            }
            if (pin < 0 || !outputGpioAllowed(pin) ||
                (pin >= 0 && (pin == stopPin || pin == startPin))) return false;
            if (!pwmPercentRange(item, "pwm_min_pct", "pwm_max_pct")) return false;
            if (strcmp(key, "glow_plug") == 0) {
                const int glowType = item["type"] | 0;
                const int glowOutputType = item["output_type"] | 0;
                if (glowType < 0 || glowType > 2) return false;
                if (glowOutputType < 0 || glowOutputType > 1) return false;
                if (glowType == 2) {
                    const int fuelPin = item["fuel_pin"] | -1;
                    const int fuelType = item["fuel_type"] | 0;
                    if (fuelType < 0 || fuelType > 2) return false;
                    if (fuelPin < 0 || !outputGpioAllowed(fuelPin) ||
                        fuelPin == stopPin || fuelPin == startPin) return false;
                    if (!intRange(item, "fuel_delay_ms", 0, 3600000) ||
                        !intRange(item, "fuel_min_us", 500, 2500) ||
                        !intRange(item, "fuel_max_us", 500, 2500) ||
                        !intRange(item, "fuel_freq_hz", 1, 100000) ||
                        !intRange(item, "fuel_res_bits", 8, 14) ||
                        !pwmPercentRange(item, "fuel_pwm_min_pct", "fuel_pwm_max_pct") ||
                        !numberRange(item, "fuel_demand_pct", 0.0f, 100.0f)) return false;
                }
            } else if (strcmp(key, "bleed_valve") == 0) {
                const int type = item["type"] | 0;
                if (type < 0 || type > 2) return false;
            } else if (strcmp(key, "throttle") == 0 ||
                       strcmp(key, "starter") == 0 ||
                       strcmp(key, "oil_pump") == 0 ||
                       strcmp(key, "cool_fan") == 0 ||
                       strcmp(key, "ab_pump") == 0 ||
                       strcmp(key, "oil_scavenge_pump") == 0 ||
                       strcmp(key, "fuel_pump2") == 0 ||
                       strcmp(key, "prop_pitch") == 0) {
                const int type = item["type"] | 0;
                if (type < 0 || type > 2) return false;
            }
        }
    }

    const char* currentSensorOwners[] = { "glow_plug", "igniter", "igniter2", "oil_pump" };
    for (const char* key : currentSensorOwners) {
        JsonVariantConst item = actuators[key];
        if (enabled(item) && (item["has_current"] | false) &&
            !requiredPinAllowed(item, "current_pin", adcGpioAllowed)) return false;
        if (!numberRange(item, "current_zero_v", 0.0f, 3.3f) ||
            !numberRange(item, "current_mv_a", 0.001f, 10000.0f)) return false;
    }
    if (!numberRange(actuators["glow_plug"], "current_ready_a", 0.0f, 1000.0f) ||
        !numberRange(actuators["igniter"], "coil_sat_a", 0.001f, 1000.0f) ||
        !numberRange(actuators["igniter2"], "coil_sat_a", 0.001f, 1000.0f) ||
        !numberRange(actuators["oil_pump"], "current_max_a", 0.0f, 1000.0f)) return false;

    JsonVariantConst cluster = doc["cluster_serial"];
    JsonVariantConst mavlink = doc["mavlink"];
    JsonVariantConst buzzer = doc["buzzer"];
    if (enabled(cluster) &&
        (!requiredPinAllowed(cluster, "tx_pin", outputGpioAllowed) ||
         !optionalPinAllowed(cluster, "rx_pin", gpioAllowed) ||
         (jsonPin(cluster, "tx_pin") >= 0 && jsonPin(cluster, "tx_pin") == jsonPin(cluster, "rx_pin")))) return false;
    if (!intRange(cluster, "baud", 9600, 921600) ||
        !intRange(cluster, "interval_ms", 10, 5000)) return false;
    if (enabled(mavlink) && !requiredPinAllowed(mavlink, "tx_pin", outputGpioAllowed)) return false;
    if (enabled(buzzer) && !requiredPinAllowed(buzzer, "pin", outputGpioAllowed)) return false;

    auto docHasDiRoleInMode = [&](const char* wantedRole, uint8_t modeBit) {
        if (!doc["di_channels"].is<JsonArrayConst>()) return false;
        for (JsonVariantConst ch : doc["di_channels"].as<JsonArrayConst>()) {
            const char* role = ch["role"] | "none";
            const uint8_t activeModes = ch["active_modes"] | (uint8_t)0x1F;
            if (strcmp(role, wantedRole) == 0 &&
                jsonPin(ch, "pin") >= 0 &&
                (activeModes & modeBit) != 0) return true;
        }
        return false;
    };

    if (hasAfterburner) {
        JsonVariantConst abTrigger = doc["ab_trigger"];
        const int abSource = abTrigger["source"] | 0;
        if (abSource < 0 || abSource > 3) return false;
        if (abSource == 2 && !requiredPinAllowed(abTrigger, "switch_pin", gpioAllowed)) return false;
        const int inputPin = jsonPin(abTrigger, "input_pin");
        if (abSource == 3 && inputPin < 0) return false;
        if (inputPin >= 0) {
            if (!((abTrigger["input_rc_pwm"] | false) ? gpioAllowed(inputPin)
                                                       : adcGpioAllowed(inputPin))) return false;
        }
        if (!intRange(abTrigger, "input_threshold", 0, 4095) ||
            !intRange(abTrigger, "input_min_us", 500, 2500) ||
            !intRange(abTrigger, "input_max_us", 500, 2500)) return false;
        if (abSource != 0 && (abTrigger["requires_arm"] | false) &&
            !docHasDiRoleInMode("ab_arm", 1u << 2) &&
            !requiredPinAllowed(abTrigger, "arm_pin", gpioAllowed)) return false;
        JsonVariantConst abFlame = doc["ab_flame"];
        if (enabled(abFlame) && !requiredPinAllowed(abFlame, "pin", adcGpioAllowed)) return false;
        if (!intRange(abFlame, "threshold", 0, 4095)) return false;
    }

    auto validDiRole = [](const char* role) {
        return strcmp(role, "none") == 0 ||
               strcmp(role, "fault") == 0 ||
               strcmp(role, "estop") == 0 ||
               strcmp(role, "inhibit_start") == 0 ||
               strcmp(role, "sequence_gate") == 0 ||
               strcmp(role, "ab_arm") == 0 ||
               strcmp(role, "ab_fire") == 0 ||
               strcmp(role, "limp_mode") == 0;
    };
    if (!doc["di_channels"].isNull() && !doc["di_channels"].is<JsonArrayConst>()) return false;
    for (JsonVariantConst ch : doc["di_channels"].as<JsonArrayConst>()) {
        if (!optionalStringFits(ch["label"], sizeof(HardwareConfig::diCh[0].label)) ||
            !optionalStringFits(ch["role"], sizeof(HardwareConfig::diCh[0].role)) ||
            !optionalStringFits(ch["fault_code"], sizeof(HardwareConfig::diCh[0].faultCode)) ||
            !optionalStringFits(ch["fault_msg"], sizeof(HardwareConfig::diCh[0].faultMsg))) return false;
        const char* role = ch["role"] | "none";
        if (!validDiRole(role)) return false;
        if ((role && strcmp(role, "none") != 0) && jsonPin(ch, "pin") < 0) return false;
        if (!optionalPinAllowed(ch, "pin", gpioAllowed) ||
            !intRange(ch, "debounce_ms", 5, 500)) return false;
        // active_modes: wrong type rejects, but out-of-range values are
        // accepted and masked to 0x1F at load (warn, don't brick the config).
        JsonVariantConst am = ch["active_modes"];
        if (!am.isNull() && !am.is<int>() && !am.is<long>() &&
            !am.is<unsigned int>() && !am.is<unsigned long>()) return false;
    }

    struct PinUse {
        int pin;
        uint8_t shareGroup;
    };
    PinUse used[96] = {};
    size_t usedCount = 0;
    auto addPin = [&](int pin, uint8_t shareGroup = 0) -> bool {
        if (pin < 0) return true;
        for (size_t i = 0; i < usedCount; i++) {
            if (used[i].pin != pin) continue;
            // SPI CLK/MISO/MOSI may be shared by SPI temperature sensors on one bus.
            if (shareGroup != 0 && used[i].shareGroup == shareGroup) return true;
            return false;
        }
        if (usedCount >= sizeof(used) / sizeof(used[0])) return false;
        used[usedCount++] = { pin, shareGroup };
        return true;
    };

    if (!addPin(stopPin) || !addPin(startPin)) return false;

    if (enabled(sensors["n1_rpm"]) && !addPin(jsonPin(sensors["n1_rpm"], "pin"))) return false;
    if (hasTwoShaft && enabled(sensors["n2_rpm"]) && !addPin(jsonPin(sensors["n2_rpm"], "pin"))) return false;
    for (const char* key : analogSensors)
        if (enabled(sensors[key]) && !addPin(jsonPin(sensors[key], "pin"))) return false;
    if (enabled(fuelFlow) && !addPin(jsonPin(fuelFlow, "pin"))) return false;
    for (const char* key : inputSensors)
        if (enabled(sensors[key]) && !addPin(jsonPin(sensors[key], "pin"))) return false;

    for (const char* key : spiSensors) {
        JsonVariantConst item = sensors[key];
        if (!enabled(item)) continue;
        if (!addPin(jsonPin(item, "clk"), 1) ||
            !addPin(jsonPin(item, "miso"), 2) ||
            !addPin(jsonPin(item, "mosi"), 3) ||
            !addPin(jsonPin(item, "cs"))) return false;
    }
    if (enabled(oilTemp)) {
        const char* chip = oilTemp["chip"] | "ntc";
        if (strcmp(chip, "ntc") == 0 || strcmp(chip, "ds18b20") == 0) {
            if (!addPin(jsonPin(oilTemp, "pin"))) return false;
        } else if (!addPin(jsonPin(oilTemp, "clk"), 1) ||
                   !addPin(jsonPin(oilTemp, "miso"), 2) ||
                   !addPin(jsonPin(oilTemp, "mosi"), 3) ||
                   !addPin(jsonPin(oilTemp, "cs"))) return false;
    }
    if (enabled(torque)) {
        if (torque["hx711"] | false) {
            if (!addPin(jsonPin(torque, "dt_pin")) ||
                !addPin(jsonPin(torque, "clk_pin"))) return false;
        } else if (!addPin(jsonPin(torque, "pin"))) return false;
    }

    for (const char* key : actuatorNames) {
        JsonVariantConst item = actuators[key];
        if (!hasAfterburner &&
            (strcmp(key, "ab_sol") == 0 || strcmp(key, "ab_pump") == 0)) continue;
        if (enabled(item) && !addPin(jsonPin(item, "pin"))) return false;
        if (strcmp(key, "glow_plug") == 0 && enabled(item) && ((item["type"] | 0) == 2) &&
            !addPin(item["fuel_pin"] | -1)) return false;
    }
    for (const char* key : currentSensorOwners) {
        JsonVariantConst item = actuators[key];
        if (enabled(item) && (item["has_current"] | false) &&
            !addPin(jsonPin(item, "current_pin"))) return false;
    }
    if (enabled(cluster) &&
        (!addPin(jsonPin(cluster, "tx_pin")) ||
         !addPin(jsonPin(cluster, "rx_pin")))) return false;
    if (enabled(mavlink) && !addPin(jsonPin(mavlink, "tx_pin"))) return false;
    if (enabled(buzzer) && !addPin(jsonPin(buzzer, "pin"))) return false;

    if (hasAfterburner) {
        JsonVariantConst abTrigger = doc["ab_trigger"];
        const int abSource = abTrigger["source"] | 0;
        if (abSource == 2 && !addPin(jsonPin(abTrigger, "switch_pin"))) return false;
        if (jsonPin(abTrigger, "input_pin") >= 0 && !addPin(jsonPin(abTrigger, "input_pin"))) return false;
        if (abSource != 0 && (abTrigger["requires_arm"] | false) &&
            !docHasDiRoleInMode("ab_arm", 1u << 2) &&
            !addPin(jsonPin(abTrigger, "arm_pin"))) return false;
        JsonVariantConst abFlame = doc["ab_flame"];
        if (enabled(abFlame) && !addPin(jsonPin(abFlame, "pin"))) return false;
    }
    for (JsonVariantConst ch : doc["di_channels"].as<JsonArrayConst>())
        if (!addPin(jsonPin(ch, "pin"))) return false;

    ChannelRegistry registry;
    if (!doc["channel_registry"].isNull()) {
        if (!registry.fromJson(doc["channel_registry"].as<JsonObjectConst>())) return false;
        for (uint8_t i = 0; i < registry.inputCount; i++) {
            const auto& ch = registry.inputs[i];
            if (ch.pin < 0) continue;
            if (ch.driver == ChannelRegistry::Analog) {
                if (!adcGpioAllowed(ch.pin)) return false;
            } else if (!gpioAllowed(ch.pin)) {
                return false;
            }
        }
        for (uint8_t i = 0; i < registry.outputCount; i++) {
            const auto& ch = registry.outputs[i];
            if (ch.pin >= 0 && (!outputGpioAllowed(ch.pin) ||
                ch.pin == stopPin || ch.pin == startPin)) return false;
        }
    }

    return true;
}
}

// ── Static member definitions ─────────────────────────────────
// Default values mirror hardware_profile.h so that a missing
// an ecu_config.json without a hardware section produces identical behaviour to the current build.

ChannelRegistry HardwareConfig::channelRegistry = {};
char  HardwareConfig::profileId[64]    = {};
char  HardwareConfig::profileDesc[64]  = {};
char  HardwareConfig::wifiPassword[64] = {};   // empty = open network; WPA2 allows 8-63 chars
int   HardwareConfig::wifiTxPowerDbm   = 8;
bool  HardwareConfig::hasAfterburner   = DEFAULT_HAS_AFTERBURNER;
bool  HardwareConfig::hasTwoShaft      = DEFAULT_HAS_N2_RPM;

// Physical controls
int   HardwareConfig::stopPin          = OT_STOP_PIN;
bool  HardwareConfig::stopActiveH      = false;  // active-low: button connects pin to GND
bool  HardwareConfig::stopPullup       = true;   // enable internal pull-up by default
int   HardwareConfig::startPin         = OT_START_PIN;
bool  HardwareConfig::startActiveH     = false;  // active-low
bool  HardwareConfig::startPullup      = true;   // enable internal pull-up by default

// Sensor feature flags
bool  HardwareConfig::hasN1Rpm         = DEFAULT_HAS_N1_RPM;
bool  HardwareConfig::hasN2Rpm         = DEFAULT_HAS_N2_RPM;
bool  HardwareConfig::hasTot           = DEFAULT_HAS_TOT;
bool  HardwareConfig::hasTit           = false;
bool  HardwareConfig::hasOilPress      = DEFAULT_HAS_OIL_PRESS;
bool  HardwareConfig::hasFlame         = DEFAULT_HAS_FLAME;
bool  HardwareConfig::hasFuelFlow      = DEFAULT_HAS_FUEL_FLOW;
bool  HardwareConfig::hasFuelPress     = false;
bool  HardwareConfig::hasP1            = DEFAULT_HAS_P1;
bool  HardwareConfig::hasP2            = DEFAULT_HAS_P2;
bool  HardwareConfig::hasThrottleInput = DEFAULT_HAS_THROTTLE_INPUT;
bool  HardwareConfig::hasIdleInput     = DEFAULT_HAS_IDLE_INPUT;
bool  HardwareConfig::hasOilTemp       = false;
bool  HardwareConfig::hasBattVoltage   = false;
bool  HardwareConfig::hasTorque        = false;

// Sensor pins & params
int   HardwareConfig::n1RpmPin         = OT_N1_RPM_PIN;
float HardwareConfig::n1RpmPpr         = OT_N1_RPM_PPR;
int   HardwareConfig::n2RpmPin         = OT_N2_RPM_PIN;
float HardwareConfig::n2RpmPpr         = OT_N2_RPM_PPR;
char  HardwareConfig::totChip[12]      = "max6675";
char  HardwareConfig::totTcType[4]     = "K";
int   HardwareConfig::totClk           = OT_TOT_CLK;
int   HardwareConfig::totCs            = OT_TOT_CS;
int   HardwareConfig::totMiso          = OT_TOT_MISO;
int   HardwareConfig::totMosi          = -1;
char  HardwareConfig::titChip[12]      = "max6675";
char  HardwareConfig::titTcType[4]     = "K";
int   HardwareConfig::titClk           = -1;
int   HardwareConfig::titCs            = -1;
int   HardwareConfig::titMiso          = -1;
int   HardwareConfig::titMosi          = -1;
int   HardwareConfig::oilPressPin      = OT_OIL_PRESS_PIN;
int   HardwareConfig::flamePin         = OT_FLAME_PIN;
int   HardwareConfig::fuelFlowPin           = OT_FUEL_FLOW_PIN;
int   HardwareConfig::fuelFlowType          = 0;
float HardwareConfig::fuelFlowPulsesPerLitre = 100.0f;
int   HardwareConfig::fuelPressPin     = OT_ADC_5;
int   HardwareConfig::p1Pin            = OT_P1_PIN;
int   HardwareConfig::p2Pin            = OT_P2_PIN;
int   HardwareConfig::throttleInputPin = OT_THROTTLE_INPUT_PIN;
bool  HardwareConfig::throttleInputRcPwm = DEFAULT_THROTTLE_INPUT_RC_PWM;
int   HardwareConfig::idleInputPin     = OT_IDLE_INPUT_PIN;
bool  HardwareConfig::idleInputRcPwm   = DEFAULT_IDLE_INPUT_RC_PWM;

char  HardwareConfig::oilTempChip[12]  = "ntc";
int   HardwareConfig::oilTempPin       = -1;
int   HardwareConfig::oilTempCs        = -1;
int   HardwareConfig::oilTempMiso      = -1;
int   HardwareConfig::oilTempMosi      = -1;
char  HardwareConfig::oilTempTcType[4] = "K";
int   HardwareConfig::oilTempResolution = 12;
float HardwareConfig::ntcBeta          = 3950.0f;
float HardwareConfig::ntcR0            = 10000.0f;
float HardwareConfig::ntcRFixed        = 10000.0f;
bool  HardwareConfig::oilTempUseRawPoly = false;
float HardwareConfig::oilTempPolyA = 0, HardwareConfig::oilTempPolyB = 0;
float HardwareConfig::oilTempPolyC = 0, HardwareConfig::oilTempPolyD = 0;
float HardwareConfig::oilTempPolyXMin = 0, HardwareConfig::oilTempPolyXMax = 4095;
int   HardwareConfig::battVoltPin      = -1;
float HardwareConfig::battVoltDivider  = 5.7f;
int   HardwareConfig::torquePin        = -1;
float HardwareConfig::torqueScale      = 30.3f;
float HardwareConfig::torqueOffset     = 0.0f;
bool  HardwareConfig::torqueHx711      = false;
int   HardwareConfig::torqueDtPin      = -1;
int   HardwareConfig::torqueClkPin     = -1;
float HardwareConfig::torqueHxScale    = 1.0f;
long  HardwareConfig::torqueHxZero     = 0;

// Actuator feature flags
bool  HardwareConfig::hasThrottle      = DEFAULT_HAS_THROTTLE;
bool  HardwareConfig::hasStarter       = DEFAULT_HAS_STARTER;
bool  HardwareConfig::hasOilPump       = DEFAULT_HAS_OIL_PUMP;
bool  HardwareConfig::hasFuelSol       = DEFAULT_HAS_FUEL_SOL;
bool  HardwareConfig::hasIgniter       = DEFAULT_HAS_IGNITER;
bool  HardwareConfig::hasIgniter2      = false;
bool  HardwareConfig::hasStarterEn     = DEFAULT_HAS_STARTER_EN;
bool  HardwareConfig::hasAbSol         = DEFAULT_HAS_AB_SOL;
bool  HardwareConfig::hasAirstarterSol = DEFAULT_HAS_AIRSTARTER_SOL;
bool  HardwareConfig::hasCoolFan       = DEFAULT_HAS_COOL_FAN;
bool  HardwareConfig::hasAbPump        = false;
bool  HardwareConfig::hasOilScavengePump = false;
bool  HardwareConfig::hasFuelPump2     = false;
bool  HardwareConfig::hasBleedValve    = false;
bool  HardwareConfig::hasPropPitch     = false;
bool  HardwareConfig::hasGlowPlug      = false;
bool  HardwareConfig::hasGlowCurrentSensor       = false;
bool  HardwareConfig::hasIgniterCurrentSensor    = false;
bool  HardwareConfig::hasIgniter2CurrentSensor   = false;
bool  HardwareConfig::hasOilPumpCurrentSensor    = false;
bool  HardwareConfig::hasGovernor      = false;
bool  HardwareConfig::hasMAVLink       = false;
bool  HardwareConfig::hasStatusLed     = DEFAULT_STATUS_LED_PIN != -1;
bool  HardwareConfig::hasClusterSerial = DEFAULT_HAS_CLUSTER_SERIAL;
bool  HardwareConfig::hasBuzzer        = false;
int   HardwareConfig::buzzerPin        = -1;

int   HardwareConfig::fuelPump2Pin     = -1;
int   HardwareConfig::fuelPump2Type    = 1;   // ledc_pwm
int   HardwareConfig::fuelPump2MinUs   = 1000;
int   HardwareConfig::fuelPump2MaxUs   = 2000;
bool  HardwareConfig::fuelPump2ActiveH = true;
int   HardwareConfig::fuelPump2FreqHz  = 10000;
int   HardwareConfig::fuelPump2ResBits = 12;
float HardwareConfig::fuelPump2PwmMinPct = 0.0f;
float HardwareConfig::fuelPump2PwmMaxPct = 100.0f;
int   HardwareConfig::bleedValveType    = 0;     // 0=on-off, 1=servo, 2=ledc_pwm
int   HardwareConfig::bleedValvePin    = -1;
bool  HardwareConfig::bleedValveActiveH = true;
int   HardwareConfig::bleedValveMinUs  = 1000;
int   HardwareConfig::bleedValveMaxUs  = 2000;
int   HardwareConfig::bleedValveFreqHz = 1000;
int   HardwareConfig::bleedValveResBits = 10;
float HardwareConfig::bleedValvePwmMinPct = 0.0f;
float HardwareConfig::bleedValvePwmMaxPct = 100.0f;
int   HardwareConfig::propPitchType    = 0;     // 0=servo, 1=ledc_pwm, 2=on-off
int   HardwareConfig::propPitchPin     = -1;
int   HardwareConfig::propPitchMinUs   = 1000;
int   HardwareConfig::propPitchMaxUs   = 2000;
int   HardwareConfig::propPitchFreqHz  = 1000;
int   HardwareConfig::propPitchResBits = 10;
float HardwareConfig::propPitchPwmMinPct = 0.0f;
float HardwareConfig::propPitchPwmMaxPct = 100.0f;
bool  HardwareConfig::propPitchActiveH = true;
int   HardwareConfig::glowPlugType     = 0;
int   HardwareConfig::glowPlugOutputType = 0;
bool  HardwareConfig::glowPlugActiveH  = true;
int   HardwareConfig::glowPlugPin      = -1;
int   HardwareConfig::glowPlugFreqHz   = 1000;
int   HardwareConfig::glowPlugResBits  = 8;
float HardwareConfig::glowPlugPwmMinPct = 0.0f;
float HardwareConfig::glowPlugPwmMaxPct = 100.0f;
int   HardwareConfig::wetGlowFuelPin       = -1;
int   HardwareConfig::wetGlowFuelType      = 0;
bool  HardwareConfig::wetGlowFuelActiveH   = true;
int   HardwareConfig::wetGlowFuelMinUs     = 1000;
int   HardwareConfig::wetGlowFuelMaxUs     = 2000;
int   HardwareConfig::wetGlowFuelFreqHz    = 1000;
int   HardwareConfig::wetGlowFuelResBits   = 10;
float HardwareConfig::wetGlowFuelPwmMinPct = 0.0f;
float HardwareConfig::wetGlowFuelPwmMaxPct = 100.0f;
float HardwareConfig::wetGlowFuelDemandPct = 100.0f;
int   HardwareConfig::wetGlowFuelDelayMs   = 8000;
int   HardwareConfig::glowCurrentPin           = -1;
float HardwareConfig::glowCurrentMvPerA        = 185.0f;
float HardwareConfig::glowCurrentZeroV         = 1.65f;
float HardwareConfig::glowCurrentReadyAmps     = 3.0f;
int   HardwareConfig::oilPumpCurrentPin        = -1;
float HardwareConfig::oilPumpCurrentMvPerA     = 100.0f;
float HardwareConfig::oilPumpCurrentZeroV      = 1.65f;
float HardwareConfig::oilPumpCurrentMaxAmps    = 0.0f;    // 0 = disabled
int   HardwareConfig::mavlinkTxPin     = -1;
int   HardwareConfig::mavlinkBaud      = 57600;
int   HardwareConfig::mavlinkIntervalMs = 100;

// Actuator pins & params
// throttleType / starterType: 0=servo  1=ledc_pwm  2=onoff
int   HardwareConfig::throttlePin         = OT_THROTTLE_PIN;
int   HardwareConfig::throttleType        = 0;     // default: servo
int   HardwareConfig::throttleMinUs       = OT_THROTTLE_SERVO_MIN_US;
int   HardwareConfig::throttleMaxUs       = OT_THROTTLE_SERVO_MAX_US;
bool  HardwareConfig::throttleInverted    = false;
bool  HardwareConfig::throttleActiveH     = true;
int   HardwareConfig::throttleLedcFreqHz  = 10000;
int   HardwareConfig::throttleLedcBits    = 12;
float HardwareConfig::throttlePwmMinPct   = 0.0f;
float HardwareConfig::throttlePwmMaxPct   = 100.0f;

int   HardwareConfig::starterPin          = OT_STARTER_MOTOR_PIN;
int   HardwareConfig::starterType         = 0;     // default: servo
int   HardwareConfig::starterMinUs        = OT_STARTER_SERVO_MIN_US;
int   HardwareConfig::starterMaxUs        = OT_STARTER_SERVO_MAX_US;
bool  HardwareConfig::starterInverted     = false;
bool  HardwareConfig::starterActiveH      = true;
int   HardwareConfig::starterLedcFreqHz   = 10000;
int   HardwareConfig::starterLedcBits     = 12;
float HardwareConfig::starterPwmMinPct    = 0.0f;
float HardwareConfig::starterPwmMaxPct    = 100.0f;
bool  HardwareConfig::starterAssistEnabled = true;   // enabled by default for servo/PWM types

int   HardwareConfig::oilPumpPin       = OT_OIL_PUMP_PIN;
#ifdef OT_OIL_PUMP_ONOFF
int   HardwareConfig::oilPumpType      = 2;   // on-off
bool  HardwareConfig::oilPumpActiveH   = OT_OIL_PUMP_ONOFF_ACTIVE_H;
int   HardwareConfig::oilPumpMinUs     = 1000;
int   HardwareConfig::oilPumpMaxUs     = 2000;
int   HardwareConfig::oilPumpFreqHz    = 10000;
int   HardwareConfig::oilPumpResBits   = 12;
float HardwareConfig::oilPumpPwmMinPct = 0.0f;
float HardwareConfig::oilPumpPwmMaxPct = 100.0f;
#else
int   HardwareConfig::oilPumpType      = 1;   // ledc_pwm
bool  HardwareConfig::oilPumpActiveH   = true;
int   HardwareConfig::oilPumpMinUs     = 1000;
int   HardwareConfig::oilPumpMaxUs     = 2000;
int   HardwareConfig::oilPumpFreqHz    = OT_OIL_PUMP_FREQ_HZ;
int   HardwareConfig::oilPumpResBits   = OT_OIL_PUMP_RES_BITS;
float HardwareConfig::oilPumpPwmMinPct = 0.0f;
float HardwareConfig::oilPumpPwmMaxPct = 100.0f;
#endif

int   HardwareConfig::fuelSolPin       = OT_FUEL_SOL_PIN;
bool  HardwareConfig::fuelSolActiveH   = OT_FUEL_SOL_ACTIVE_H;

int   HardwareConfig::igniterPin       = OT_IGNITER_PIN;
bool  HardwareConfig::igniterActiveH   = OT_IGNITER_ACTIVE_H;
#ifdef OT_IGNITER_PWM
bool  HardwareConfig::igniterPwm       = true;
int   HardwareConfig::igniterDwellMs   = OT_IGNITER_DWELL_MS;
int   HardwareConfig::igniterRestMs    = OT_IGNITER_REST_MS;
#else
bool  HardwareConfig::igniterPwm       = false;
int   HardwareConfig::igniterDwellMs   = 6;
int   HardwareConfig::igniterRestMs    = 3;
#endif
bool  HardwareConfig::igniterCoil              = false;
float HardwareConfig::igniterCoilSatAmps       = 8.0f;
int   HardwareConfig::igniterCurrentPin        = -1;
float HardwareConfig::igniterCurrentMvPerA     = 100.0f;
float HardwareConfig::igniterCurrentZeroV      = 1.65f;

int   HardwareConfig::starterEnPin     = OT_STARTER_EN_PIN;
bool  HardwareConfig::starterEnActiveH = OT_STARTER_EN_ACTIVE_H;
int   HardwareConfig::starterEnDelayMs = 1000;  // 1 s default

int   HardwareConfig::igniter2Pin      = -1;
bool  HardwareConfig::igniter2ActiveH  = true;
bool  HardwareConfig::igniter2Pwm      = false;
int   HardwareConfig::igniter2DwellMs  = 6;
int   HardwareConfig::igniter2RestMs   = 3;
bool  HardwareConfig::igniter2Coil             = false;
float HardwareConfig::igniter2CoilSatAmps      = 8.0f;
int   HardwareConfig::igniter2CurrentPin       = -1;
float HardwareConfig::igniter2CurrentMvPerA    = 100.0f;
float HardwareConfig::igniter2CurrentZeroV     = 1.65f;

int   HardwareConfig::abSolPin         = OT_AB_SOL_PIN;
bool  HardwareConfig::abSolActiveH     = OT_AB_SOL_ACTIVE_H;
int   HardwareConfig::airstarterSolPin = OT_AIRSTARTER_SOL_PIN;
bool  HardwareConfig::airstarterSolActiveH = true;

int   HardwareConfig::coolFanPin       = OT_COOL_FAN_PIN;
int   HardwareConfig::coolFanType      = 2;   // on-off default
int   HardwareConfig::coolFanMinUs     = 1000;
int   HardwareConfig::coolFanMaxUs     = 2000;
bool  HardwareConfig::coolFanActiveH   = true;
int   HardwareConfig::coolFanFreqHz    = 10000;
int   HardwareConfig::coolFanResBits   = 12;
float HardwareConfig::coolFanPwmMinPct = 0.0f;
float HardwareConfig::coolFanPwmMaxPct = 100.0f;

int   HardwareConfig::abPumpPin        = -1;
int   HardwareConfig::abPumpType       = 2;   // on-off default
int   HardwareConfig::abPumpMinUs      = 1000;
int   HardwareConfig::abPumpMaxUs      = 2000;
bool  HardwareConfig::abPumpActiveH    = true;
int   HardwareConfig::abPumpFreqHz     = 10000;
int   HardwareConfig::abPumpResBits    = 12;
float HardwareConfig::abPumpPwmMinPct  = 0.0f;
float HardwareConfig::abPumpPwmMaxPct  = 100.0f;

int   HardwareConfig::oilScavPumpPin     = -1;
int   HardwareConfig::oilScavPumpType    = 2;
int   HardwareConfig::oilScavPumpMinUs   = 1000;
int   HardwareConfig::oilScavPumpMaxUs   = 2000;
bool  HardwareConfig::oilScavPumpActiveH = true;
int   HardwareConfig::oilScavPumpFreqHz  = 10000;
int   HardwareConfig::oilScavPumpResBits = 12;
float HardwareConfig::oilScavPumpPwmMinPct = 0.0f;
float HardwareConfig::oilScavPumpPwmMaxPct = 100.0f;

int   HardwareConfig::abTriggerSource    = 0;   // 0=manual
bool  HardwareConfig::abRequiresArmSwitch= false;
int   HardwareConfig::abArmSwitchPin     = -1;
bool  HardwareConfig::abArmSwitchActiveH = false;
int   HardwareConfig::abSwitchPin        = -1;
bool  HardwareConfig::abSwitchActiveH    = false;
int   HardwareConfig::abInputPin         = -1;
bool  HardwareConfig::abInputRcPwm       = false;
int   HardwareConfig::abInputMinUs       = 1000;
int   HardwareConfig::abInputMaxUs       = 2000;
int   HardwareConfig::abInputThreshold   = 2048;

bool  HardwareConfig::hasAbFlame         = false;
int   HardwareConfig::abFlamePin         = -1;
int   HardwareConfig::abFlameThreshold   = 500;

int   HardwareConfig::statusLedPin     = DEFAULT_STATUS_LED_PIN;
int   HardwareConfig::statusLedType    = DEFAULT_STATUS_LED_TYPE;
int   HardwareConfig::statusLedMode    = DEFAULT_STATUS_LED_MODE;
uint32_t HardwareConfig::statusLedStandbyColor  = DEFAULT_STATUS_LED_STANDBY_COLOR;
uint32_t HardwareConfig::statusLedStartupColor  = DEFAULT_STATUS_LED_STARTUP_COLOR;
uint32_t HardwareConfig::statusLedRunningColor  = DEFAULT_STATUS_LED_RUNNING_COLOR;
uint32_t HardwareConfig::statusLedShutdownColor = DEFAULT_STATUS_LED_SHUTDOWN_COLOR;
uint32_t HardwareConfig::statusLedBlinkColor    = DEFAULT_STATUS_LED_BLINK_COLOR;

// Cluster serial
int   HardwareConfig::clusterTxPin     = OT_CLUSTER_TX_PIN;
int   HardwareConfig::clusterRxPin     = -1;
int   HardwareConfig::clusterBaud      = OT_CLUSTER_BAUD;
int   HardwareConfig::clusterIntervalMs= OT_CLUSTER_INTERVAL_MS;

// Controller feature flags
bool  HardwareConfig::hasOilLoop       = DEFAULT_HAS_OIL_LOOP;
bool  HardwareConfig::hasThrottleSlew  = DEFAULT_HAS_THROTTLE_SLEW;
bool  HardwareConfig::hasDynamicIdle   = DEFAULT_HAS_DYNAMIC_IDLE;
HardwareConfig::OilLoopDef HardwareConfig::oilLoops[HardwareConfig::MAX_OIL_LOOPS] = {};
uint8_t HardwareConfig::oilLoopCount = 0;

// Safety enables
bool  HardwareConfig::safetyOverspeed  = DEFAULT_SAFETY_OVERSPEED;
bool  HardwareConfig::safetyOvertemp   = DEFAULT_SAFETY_OVERTEMP;
bool  HardwareConfig::safetyLowOil     = DEFAULT_SAFETY_LOW_OIL;
bool  HardwareConfig::safetyOilZero    = DEFAULT_SAFETY_OIL_ZERO;
bool  HardwareConfig::safetyFlameout   = DEFAULT_SAFETY_FLAMEOUT;
bool  HardwareConfig::safetyHotStart   = false;
bool  HardwareConfig::safetyTitOvertemp  = false;
bool  HardwareConfig::safetyOilTempHigh  = false;
bool  HardwareConfig::safetyFuelPressLow = false;
bool  HardwareConfig::safetyBattLow      = false;
bool  HardwareConfig::safetySurge        = false;

// Channel display labels
char HardwareConfig::labelTot[32]       = "TOT";
char HardwareConfig::labelTit[32]       = "TIT";
char HardwareConfig::labelN1[32]        = "N1";
char HardwareConfig::labelN2[32]        = "N2";
char HardwareConfig::labelOilPress[32]  = "Oil Press";
char HardwareConfig::labelOilTemp[32]   = "Oil Temp";
char HardwareConfig::labelP1[32]        = "P1";
char HardwareConfig::labelP2[32]        = "P2";
char HardwareConfig::labelFuelPress[32] = "Fuel Press";
char HardwareConfig::labelFuelFlow[32]  = "Fuel Flow";
char HardwareConfig::labelStop[32]      = "Stop";
char HardwareConfig::labelStart[32]     = "Start";
char HardwareConfig::labelAbArm[32]     = "AB Arm";

// General-purpose digital input channels
HardwareConfig::DiChannel HardwareConfig::diCh[HardwareConfig::MAX_DI] = {};

// Sequences — block order and delays come from OT_STARTUP_SEQ /
// OT_SHUTDOWN_SEQ (+ OT_*_DELAY_MS) in hardware_profile.h.
char  HardwareConfig::startupSeq[MAX_SEQ_BLOCKS][24] = {
#define OT_BLOCK(name) #name,
    OT_STARTUP_SEQ
#undef OT_BLOCK
};
int   HardwareConfig::startupSeqLen    = kProfileStartupSeqLen;
int   HardwareConfig::startupDelayMs[MAX_SEQ_BLOCKS] = OT_STARTUP_DELAY_MS;
uint8_t HardwareConfig::startupIgnitionTarget[MAX_SEQ_BLOCKS] = {};
HardwareConfig::SeqSideAction HardwareConfig::startupEnterActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};
HardwareConfig::SeqSideAction HardwareConfig::startupExitActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};

char  HardwareConfig::shutdownSeq[MAX_SEQ_BLOCKS][24] = {
#define OT_BLOCK(name) #name,
    OT_SHUTDOWN_SEQ
#undef OT_BLOCK
};
int   HardwareConfig::shutdownSeqLen   = kProfileShutdownSeqLen;
int   HardwareConfig::shutdownDelayMs[MAX_SEQ_BLOCKS] = OT_SHUTDOWN_DELAY_MS;
uint8_t HardwareConfig::shutdownIgnitionTarget[MAX_SEQ_BLOCKS] = {};
HardwareConfig::SeqSideAction HardwareConfig::shutdownEnterActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};
HardwareConfig::SeqSideAction HardwareConfig::shutdownExitActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};

char  HardwareConfig::abSeq[MAX_SEQ_BLOCKS][24]    = {};
int   HardwareConfig::abSeqLen                     = 0;
int   HardwareConfig::abDelayMs[MAX_SEQ_BLOCKS]    = {};
uint8_t HardwareConfig::abIgnitionTarget[MAX_SEQ_BLOCKS] = {};
HardwareConfig::SeqSideAction HardwareConfig::abEnterActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};
HardwareConfig::SeqSideAction HardwareConfig::abExitActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};
char  HardwareConfig::abShutSeq[MAX_SEQ_BLOCKS][24]= {};
int   HardwareConfig::abShutSeqLen                 = 0;
int   HardwareConfig::abShutDelayMs[MAX_SEQ_BLOCKS]= {};
uint8_t HardwareConfig::abShutIgnitionTarget[MAX_SEQ_BLOCKS] = {};
HardwareConfig::SeqSideAction HardwareConfig::abShutEnterActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};
HardwareConfig::SeqSideAction HardwareConfig::abShutExitActions[MAX_SEQ_BLOCKS][MAX_SEQ_SIDE_ACTIONS] = {};
HardwareConfig::CustomBlockDef HardwareConfig::customBlocks[MAX_CUSTOM_BLOCKS] = {};
int HardwareConfig::customBlockCount = 0;

// ── Load ──────────────────────────────────────────────────────
static void inhibitStartForHardwareConfigFailure(const char* reason, bool storageFault = false) {
    auto& ed = EngineData::instance();
    ed.configLocked = true;
    ed.configStorageFault = storageFault;
    strncpy(ed.faultDescription, reason, sizeof(ed.faultDescription) - 1);
    ed.faultDescription[sizeof(ed.faultDescription) - 1] = '\0';
}

void HardwareConfig::load() {
    applyDefaults();
    EngineData::instance().configLocked = false;
    EngineData::instance().configStorageFault = false;

    static constexpr const char* BAK_PATH = "/ecu_config.bak";
    if (!LittleFS.exists(PATH) && LittleFS.exists(BAK_PATH)) {
        if (LittleFS.rename(BAK_PATH, PATH)) {
            Serial.println("[HWCfg] Recovered ecu_config.json from backup");
        }
    }

    // First boot with no ecu_config.json seeds the file from the compiled
    // hardware_profile.h defaults (applyDefaults() above + save() below).
    // Factory reset regenerates from the same defaults (no factory_config.json
    // ships), so hardware_profile.h is the single source of default topology.
    if (!LittleFS.exists(PATH)) {
        Serial.println("[HWCfg] No ecu_config.json - using compiled defaults, generating file");
        if (!save()) {
            inhibitStartForHardwareConfigFailure(
                "Cannot start: hardware configuration storage is unavailable.", true);
        }
        return;
    }

    File f = LittleFS.open(PATH, "r");
    if (!f) {
        Serial.println("[HWCfg] Failed to open ecu_config.json - using defaults");
        inhibitStartForHardwareConfigFailure(
            "Cannot start: failed to read the hardware configuration.", true);
        return;
    }
    JsonDocument fullDoc;
    DeserializationError err = deserializeJson(fullDoc, f);
    f.close();
    if (err) {
        Serial.printf("[HWCfg] JSON parse error: %s - using defaults\n", err.c_str());
        inhibitStartForHardwareConfigFailure(
            "Cannot start: the hardware configuration file is corrupted.", true);
        return;
    }

    JsonDocument workDoc;
    if (fullDoc[SECTION].is<JsonObject>()) {
        workDoc.set(fullDoc[SECTION]);
    } else {
        Serial.println("[HWCfg] Hardware section missing - adding compiled defaults");
        if (!save()) {
            inhibitStartForHardwareConfigFailure(
                "Cannot start: no stored hardware configuration is available.", true);
        }
        return;
    }

    const char* id = workDoc["profile_id"] | "";
    if (!id[0]) {
        inhibitStartForHardwareConfigFailure(
            "Cannot start: stored hardware profile ID is missing.");
        Serial.println("[HWCfg] Hardware profile ID is missing - START inhibited");
        return;
    }
    if (storedHardwarePlatformMismatch(workDoc)) {
        Serial.printf("[HWCfg] Stored hardware platform %s does not match firmware %s - regenerating safe defaults\n",
                      workDoc["platform"] | "(unset)", currentPlatformName());
        applyDefaults();
        if (!save()) {
            inhibitStartForHardwareConfigFailure(
                "Cannot start: hardware configuration could not be saved to storage.", true);
            Serial.println("[HWCfg] Platform migration save failed - START inhibited");
        }
        return;
    }
    normalizeS3StatusLedDefault(workDoc);
    if (!validatePlatformPins(workDoc)) {
        inhibitStartForHardwareConfigFailure(
            "Cannot start: stored hardware uses invalid or unsafe GPIO assignments.");
        Serial.println("[HWCfg] Hardware GPIO validation failed - START inhibited");
        return;
    }
    if (!workDoc["channel_registry"].isNull() &&
        (workDoc["channel_registry"]["version"] | 0) > CHANNEL_REGISTRY_VERSION) {
        inhibitStartForHardwareConfigFailure(
            "Cannot start: hardware channel registry was written by newer firmware.");
        Serial.println("[HWCfg] Future channel registry version - START inhibited");
        return;
    }
    _fromDoc(workDoc);
    Serial.printf("[HWCfg] Loaded OK - profile: %s\n", profileId);
}

// ── Save ──────────────────────────────────────────────────────
bool HardwareConfig::save() {
    static constexpr const char* TMP_PATH = "/ecu_config.hw.tmp";
    static constexpr const char* BAK_PATH = "/ecu_config.bak";
    if (!Config::acquireStorageWrite()) {
        Serial.println("[HWCfg] Timed out waiting to write ecu_config.json");
        return false;
    }
    struct StorageRelease {
        ~StorageRelease() { Config::releaseStorageWrite(); }
    } release;
    // Read-modify-write: preserve other sections (settings etc.)
    JsonDocument fullDoc;
    File fr = LittleFS.open(PATH, "r");
    if (fr) {
        DeserializationError err = deserializeJson(fullDoc, fr);
        fr.close();
        if (err) {
            Serial.printf("[HWCfg] Refusing to overwrite unreadable ecu_config.json: %s\n",
                          err.c_str());
            return false;
        }
    }

    JsonDocument hwDoc;
    _toDoc(hwDoc);
    fullDoc[SECTION].set(hwDoc);
    if (fullDoc["settings"].is<JsonObject>())
        fullDoc["settings"]["profile_id"] = profileId;

    File fw = LittleFS.open(TMP_PATH, "w");
    if (!fw) {
        Serial.println("[HWCfg] Failed to open ecu_config.hw.tmp for write");
        return false;
    }
    size_t expected = measureJsonPretty(fullDoc);
    size_t written = serializeJsonPretty(fullDoc, fw);
    fw.close();
    if (written != expected) {
        LittleFS.remove(TMP_PATH);
        Serial.println("[HWCfg] Incomplete write to ecu_config.hw.tmp");
        return false;
    }
    LittleFS.remove(BAK_PATH);
    bool hadOriginal = LittleFS.exists(PATH);
    if (hadOriginal && !LittleFS.rename(PATH, BAK_PATH)) {
        LittleFS.remove(TMP_PATH);
        Serial.println("[HWCfg] failed to preserve previous ecu_config.json");
        return false;
    }
    if (!LittleFS.rename(TMP_PATH, PATH)) {
        Serial.println("[HWCfg] rename ecu_config.hw.tmp failed");
        if (hadOriginal) LittleFS.rename(BAK_PATH, PATH);
        LittleFS.remove(TMP_PATH);
        LittleFS.remove(BAK_PATH);
        return false;
    }
    if (hadOriginal) LittleFS.remove(BAK_PATH);
    return true;
}

// ── Apply defaults ─────────────────────────────────────────────
// Called before load() to seed all values from hardware_profile.h.
// Static member initialisers already handle this at program start;
// this function is used when resetting to defaults at runtime.
void HardwareConfig::applyDefaults() {
    strncpy(profileId,   OT_PROFILE_ID,   sizeof(profileId)   - 1);
    strncpy(profileDesc, OT_PROFILE_DESC, sizeof(profileDesc) - 1);
    hasAfterburner = DEFAULT_HAS_AFTERBURNER;
    hasTwoShaft    = DEFAULT_HAS_N2_RPM;

    stopPin      = OT_STOP_PIN;  stopActiveH  = false;  stopPullup  = true;
    startPin     = OT_START_PIN; startActiveH = false;  startPullup = true;

    hasN1Rpm  = DEFAULT_HAS_N1_RPM; hasN2Rpm = DEFAULT_HAS_N2_RPM;
    hasTot = DEFAULT_HAS_TOT; hasTit = false;
    hasOilPress = DEFAULT_HAS_OIL_PRESS; hasFlame = DEFAULT_HAS_FLAME;
    hasFuelFlow = DEFAULT_HAS_FUEL_FLOW; hasFuelPress = false;
    hasP1 = DEFAULT_HAS_P1; hasP2 = DEFAULT_HAS_P2;
    hasThrottleInput = DEFAULT_HAS_THROTTLE_INPUT; hasIdleInput = DEFAULT_HAS_IDLE_INPUT;
    hasOilTemp = false; hasBattVoltage = false; hasTorque = false;

    n1RpmPin  = OT_N1_RPM_PIN; n1RpmPpr = OT_N1_RPM_PPR;
    n2RpmPin  = OT_N2_RPM_PIN;  n2RpmPpr  = OT_N2_RPM_PPR;
    strncpy(totChip, "max6675", sizeof(totChip) - 1);
    strncpy(totTcType, "K", sizeof(totTcType) - 1);
    totClk    = OT_TOT_CLK; totCs = OT_TOT_CS; totMiso = OT_TOT_MISO; totMosi = -1;
    strncpy(titChip, "max6675", sizeof(titChip) - 1);
    strncpy(titTcType, "K", sizeof(titTcType) - 1);
    titClk = -1; titCs = -1; titMiso = -1; titMosi = -1;
    oilPressPin = OT_OIL_PRESS_PIN; flamePin = OT_FLAME_PIN;
    fuelFlowPin = OT_FUEL_FLOW_PIN; fuelFlowType = 0; fuelFlowPulsesPerLitre = 100.0f;
    fuelPressPin = OT_ADC_5; p1Pin = OT_P1_PIN; p2Pin = OT_P2_PIN;
    throttleInputPin = OT_THROTTLE_INPUT_PIN; throttleInputRcPwm = DEFAULT_THROTTLE_INPUT_RC_PWM;
    idleInputPin     = OT_IDLE_INPUT_PIN;     idleInputRcPwm     = DEFAULT_IDLE_INPUT_RC_PWM;

    strncpy(oilTempChip, "ntc", sizeof(oilTempChip) - 1);
    oilTempPin = -1; oilTempCs = -1; oilTempMiso = -1; oilTempMosi = -1;
    strncpy(oilTempTcType, "K", sizeof(oilTempTcType) - 1);
    oilTempResolution = 12;
    ntcBeta = 3950.0f; ntcR0 = 10000.0f; ntcRFixed = 10000.0f;
    oilTempUseRawPoly = false;
    oilTempPolyA = oilTempPolyB = oilTempPolyC = oilTempPolyD = 0.0f;
    oilTempPolyXMin = 0.0f; oilTempPolyXMax = 4095.0f;
    battVoltPin = -1; battVoltDivider = 5.7f;
    torquePin = -1; torqueScale = 30.3f; torqueOffset = 0.0f;
    torqueHx711 = false; torqueDtPin = -1; torqueClkPin = -1;
    torqueHxScale = 1.0f; torqueHxZero = 0;

    hasThrottle = DEFAULT_HAS_THROTTLE; hasStarter = DEFAULT_HAS_STARTER;
    hasOilPump = DEFAULT_HAS_OIL_PUMP;
    hasFuelSol  = DEFAULT_HAS_FUEL_SOL; hasIgniter = DEFAULT_HAS_IGNITER;
    hasIgniter2 = false; hasStarterEn = DEFAULT_HAS_STARTER_EN;
    hasAbSol = DEFAULT_HAS_AB_SOL; hasAirstarterSol = DEFAULT_HAS_AIRSTARTER_SOL;
    hasCoolFan = DEFAULT_HAS_COOL_FAN;
    hasAbPump = false; hasFuelPump2 = false; hasBleedValve = false;
    hasPropPitch = false; hasGlowPlug = false;
    hasGlowCurrentSensor = false; hasIgniterCurrentSensor = false;
    hasIgniter2CurrentSensor = false; hasOilPumpCurrentSensor = false;
    hasGovernor = false; hasMAVLink = false;
    hasStatusLed = DEFAULT_STATUS_LED_PIN != -1; hasClusterSerial = DEFAULT_HAS_CLUSTER_SERIAL;
    hasBuzzer = false; buzzerPin = -1;

    fuelPump2Pin = -1; fuelPump2Type = 1; fuelPump2MinUs = 1000; fuelPump2MaxUs = 2000;
    fuelPump2ActiveH = true; fuelPump2FreqHz = 10000; fuelPump2ResBits = 12;
    fuelPump2PwmMinPct = 0.0f; fuelPump2PwmMaxPct = 100.0f;
    bleedValveType = 0; bleedValvePin = -1; bleedValveActiveH = true;
    bleedValveMinUs = 1000; bleedValveMaxUs = 2000; bleedValveFreqHz = 1000; bleedValveResBits = 10;
    bleedValvePwmMinPct = 0.0f; bleedValvePwmMaxPct = 100.0f;
    propPitchType = 0; propPitchPin = -1; propPitchMinUs = 1000; propPitchMaxUs = 2000;
    propPitchFreqHz = 1000; propPitchResBits = 10; propPitchActiveH = true;
    propPitchPwmMinPct = 0.0f; propPitchPwmMaxPct = 100.0f;
    glowPlugType = 0; glowPlugOutputType = 0; glowPlugActiveH = true;
    glowPlugPin = -1; glowPlugFreqHz = 1000; glowPlugResBits = 8;
    glowPlugPwmMinPct = 0.0f; glowPlugPwmMaxPct = 100.0f;
    wetGlowFuelPin = -1; wetGlowFuelType = 0; wetGlowFuelActiveH = true;
    wetGlowFuelMinUs = 1000; wetGlowFuelMaxUs = 2000;
    wetGlowFuelFreqHz = 1000; wetGlowFuelResBits = 10;
    wetGlowFuelPwmMinPct = 0.0f; wetGlowFuelPwmMaxPct = 100.0f;
    wetGlowFuelDemandPct = 100.0f; wetGlowFuelDelayMs = 8000;
    glowCurrentPin = -1; glowCurrentMvPerA = 185.0f; glowCurrentZeroV = 1.65f; glowCurrentReadyAmps = 3.0f;
    oilPumpCurrentPin = -1; oilPumpCurrentMvPerA = 100.0f; oilPumpCurrentZeroV = 1.65f; oilPumpCurrentMaxAmps = 0.0f;
    mavlinkTxPin = -1; mavlinkBaud = 57600; mavlinkIntervalMs = 100;

    throttlePin        = OT_THROTTLE_PIN;
    throttleType       = 0; throttleInverted = false; throttleActiveH = true;
    throttleMinUs      = OT_THROTTLE_SERVO_MIN_US;
    throttleMaxUs      = OT_THROTTLE_SERVO_MAX_US;
    throttleLedcFreqHz = 10000; throttleLedcBits = 12;
    throttlePwmMinPct = 0.0f; throttlePwmMaxPct = 100.0f;

    starterPin        = OT_STARTER_MOTOR_PIN;
    starterType       = 0; starterInverted = false; starterActiveH = true;
    starterMinUs      = OT_STARTER_SERVO_MIN_US;
    starterMaxUs      = OT_STARTER_SERVO_MAX_US;
    starterLedcFreqHz = 10000; starterLedcBits = 12;
    starterPwmMinPct = 0.0f; starterPwmMaxPct = 100.0f;

    oilPumpPin     = OT_OIL_PUMP_PIN;
    oilPumpMinUs   = 1000; oilPumpMaxUs = 2000;
    oilPumpPwmMinPct = 0.0f; oilPumpPwmMaxPct = 100.0f;
#ifdef OT_OIL_PUMP_ONOFF
    oilPumpType    = 2;   // on-off
    oilPumpActiveH = OT_OIL_PUMP_ONOFF_ACTIVE_H;
    oilPumpFreqHz  = 10000;
    oilPumpResBits = 12;
#else
    oilPumpType    = 1;   // ledc_pwm
    oilPumpActiveH = true;
    oilPumpFreqHz  = OT_OIL_PUMP_FREQ_HZ;
    oilPumpResBits = OT_OIL_PUMP_RES_BITS;
#endif

    fuelSolPin     = OT_FUEL_SOL_PIN;
    fuelSolActiveH = OT_FUEL_SOL_ACTIVE_H;

    igniterPin     = OT_IGNITER_PIN;
    igniterActiveH = OT_IGNITER_ACTIVE_H;
#ifdef OT_IGNITER_PWM
    igniterPwm     = true;
    igniterDwellMs = OT_IGNITER_DWELL_MS;
    igniterRestMs  = OT_IGNITER_REST_MS;
#else
    igniterPwm     = false;
    igniterDwellMs = 6;
    igniterRestMs  = 3;
#endif
    igniterCoil = false; igniterCoilSatAmps = 8.0f;
    igniterCurrentPin = -1; igniterCurrentMvPerA = 100.0f; igniterCurrentZeroV = 1.65f;

    starterEnPin     = OT_STARTER_EN_PIN;
    starterEnActiveH = OT_STARTER_EN_ACTIVE_H;
    starterEnDelayMs = 1000;             // mirror static-init default (was missing here)
    starterAssistEnabled = true;         // mirror static-init default; sanitizeForHardware
                                         // disables it if no starter + N1 sensor

    igniter2Pin = -1; igniter2ActiveH = true; igniter2Pwm = false;
    igniter2DwellMs = 6; igniter2RestMs = 3;
    igniter2Coil = false; igniter2CoilSatAmps = 8.0f;
    igniter2CurrentPin = -1; igniter2CurrentMvPerA = 100.0f; igniter2CurrentZeroV = 1.65f;

    abSolPin = OT_AB_SOL_PIN; abSolActiveH = OT_AB_SOL_ACTIVE_H;
    airstarterSolPin = OT_AIRSTARTER_SOL_PIN; airstarterSolActiveH = true;

    coolFanPin = OT_COOL_FAN_PIN; coolFanType = 2; coolFanMinUs = 1000; coolFanMaxUs = 2000;
    coolFanActiveH = true; coolFanFreqHz = 10000; coolFanResBits = 12;
    coolFanPwmMinPct = 0.0f; coolFanPwmMaxPct = 100.0f;

    abPumpPin = -1; abPumpType = 2; abPumpMinUs = 1000; abPumpMaxUs = 2000;
    abPumpActiveH = true; abPumpFreqHz = 10000; abPumpResBits = 12;
    abPumpPwmMinPct = 0.0f; abPumpPwmMaxPct = 100.0f;

    hasOilScavengePump = false;
    oilScavPumpPin     = -1;
    oilScavPumpType    = 2;
    oilScavPumpMinUs   = 1000;
    oilScavPumpMaxUs   = 2000;
    oilScavPumpActiveH = true;
    oilScavPumpFreqHz  = 10000;
    oilScavPumpResBits = 12;
    oilScavPumpPwmMinPct = 0.0f; oilScavPumpPwmMaxPct = 100.0f;

    abTriggerSource     = 0;
    abRequiresArmSwitch = false;
    abArmSwitchPin      = -1;
    abArmSwitchActiveH  = false;
    abSwitchPin         = -1;
    abSwitchActiveH     = false;
    abInputPin          = -1;
    abInputRcPwm        = false;
    abInputMinUs        = 1000;
    abInputMaxUs        = 2000;
    abInputThreshold    = 2048;
    hasAbFlame          = false;
    abFlamePin          = -1;
    abFlameThreshold    = 500;

    wifiTxPowerDbm = 8;                  // mirror static-init default (was missing here)

    statusLedPin = DEFAULT_STATUS_LED_PIN;
    statusLedType = DEFAULT_STATUS_LED_TYPE;
    statusLedMode = DEFAULT_STATUS_LED_MODE;
    statusLedStandbyColor  = DEFAULT_STATUS_LED_STANDBY_COLOR;
    statusLedStartupColor  = DEFAULT_STATUS_LED_STARTUP_COLOR;
    statusLedRunningColor  = DEFAULT_STATUS_LED_RUNNING_COLOR;
    statusLedShutdownColor = DEFAULT_STATUS_LED_SHUTDOWN_COLOR;
    statusLedBlinkColor    = DEFAULT_STATUS_LED_BLINK_COLOR;

    // Labels and DI channels belong to the previous engine profile — a
    // defaults reset must not retain stale safety inputs (estop, fault,
    // inhibit_start) or display names.
    auto resetLabel = [](char* dst, size_t len, const char* value) {
        strncpy(dst, value, len - 1);
        dst[len - 1] = '\0';
    };
    resetLabel(labelTot,       sizeof(labelTot),       "TOT");
    resetLabel(labelTit,       sizeof(labelTit),       "TIT");
    resetLabel(labelN1,        sizeof(labelN1),        "N1");
    resetLabel(labelN2,        sizeof(labelN2),        "N2");
    resetLabel(labelOilPress,  sizeof(labelOilPress),  "Oil Press");
    resetLabel(labelOilTemp,   sizeof(labelOilTemp),   "Oil Temp");
    resetLabel(labelP1,        sizeof(labelP1),        "P1");
    resetLabel(labelP2,        sizeof(labelP2),        "P2");
    resetLabel(labelFuelPress, sizeof(labelFuelPress), "Fuel Press");
    resetLabel(labelFuelFlow,  sizeof(labelFuelFlow),  "Fuel Flow");
    resetLabel(labelStop,      sizeof(labelStop),      "Stop");
    resetLabel(labelStart,     sizeof(labelStart),     "Start");
    resetLabel(labelAbArm,     sizeof(labelAbArm),     "AB Arm");
    for (int i = 0; i < MAX_DI; i++) diCh[i] = DiChannel{};

    clusterTxPin    = OT_CLUSTER_TX_PIN;
    clusterRxPin    = -1;
    clusterBaud     = OT_CLUSTER_BAUD;
    clusterIntervalMs = OT_CLUSTER_INTERVAL_MS;

    hasOilLoop      = DEFAULT_HAS_OIL_LOOP;
    hasThrottleSlew = DEFAULT_HAS_THROTTLE_SLEW;
    hasDynamicIdle  = DEFAULT_HAS_DYNAMIC_IDLE;
    oilLoopCount = 0;
    for (int i = 0; i < MAX_OIL_LOOPS; i++) oilLoops[i] = OilLoopDef{};
    // hardware_profile.h compile guards enforce the other controller
    // dependencies; dynamic idle's RPM requirement is only checked here.
    if (hasDynamicIdle && !hasN1Rpm && !hasN2Rpm) hasDynamicIdle = false;

    safetyOverspeed = DEFAULT_SAFETY_OVERSPEED;
    safetyOvertemp  = DEFAULT_SAFETY_OVERTEMP;
    safetyLowOil    = DEFAULT_SAFETY_LOW_OIL;
    safetyOilZero   = DEFAULT_SAFETY_OIL_ZERO;
    safetyFlameout  = DEFAULT_SAFETY_FLAMEOUT;
    safetyHotStart      = false;
    safetyTitOvertemp   = false;
    safetyOilTempHigh   = false;
    safetyFuelPressLow  = false;
    safetyBattLow       = false;
    safetySurge         = false;

    // Block order and per-block delays come from OT_STARTUP_SEQ /
    // OT_SHUTDOWN_SEQ and OT_STARTUP_DELAY_MS / OT_SHUTDOWN_DELAY_MS.
    startupSeqLen = kProfileStartupSeqLen;
    memset(startupSeq, 0, sizeof(startupSeq));
    memset(startupDelayMs, 0, sizeof(startupDelayMs));
    memset(startupIgnitionTarget, 0, sizeof(startupIgnitionTarget));
    clearSeqSideActions(startupEnterActions);
    clearSeqSideActions(startupExitActions);
    for (int i = 0; i < startupSeqLen; i++) {
        strncpy(startupSeq[i], kProfileStartupSeq[i], sizeof(startupSeq[i]) - 1);
        if (i < kProfileStartupDelayLen) startupDelayMs[i] = kProfileStartupDelayMs[i];
    }

    shutdownSeqLen = kProfileShutdownSeqLen;
    memset(shutdownSeq, 0, sizeof(shutdownSeq));
    memset(shutdownDelayMs, 0, sizeof(shutdownDelayMs));
    memset(shutdownIgnitionTarget, 0, sizeof(shutdownIgnitionTarget));
    clearSeqSideActions(shutdownEnterActions);
    clearSeqSideActions(shutdownExitActions);
    for (int i = 0; i < shutdownSeqLen; i++) {
        strncpy(shutdownSeq[i], kProfileShutdownSeq[i], sizeof(shutdownSeq[i]) - 1);
        if (i < kProfileShutdownDelayLen) shutdownDelayMs[i] = kProfileShutdownDelayMs[i];
    }

    // AB ignition: check conditions -> open solenoid -> start pump -> torch spike -> confirm flame -> stabilize.
    // Only seed when an afterburner is fitted; a no-AB build leaves the AB
    // sequences empty rather than carrying an orphaned sequence for hardware
    // that isn't there (keeps a minimal profile truly minimal).
    const char* defAbIgn[] = {
        "ABCheckReady","ABSolOpen","ABPumpOn","ABIgnite","ABFlameConfirm","ABStabilize"
    };
    abSeqLen = hasAfterburner ? 6 : 0;
    memset(abSeq, 0, sizeof(abSeq));
    memset(abDelayMs, 0, sizeof(abDelayMs));
    memset(abIgnitionTarget, 0, sizeof(abIgnitionTarget));
    clearSeqSideActions(abEnterActions);
    clearSeqSideActions(abExitActions);
    for (int i = 0; i < abSeqLen; i++)
        strncpy(abSeq[i], defAbIgn[i], sizeof(abSeq[i]) - 1);

    // AB shutdown: close solenoid first, then cut pump
    const char* defAbShut[] = { "ABSolClose", "ABPumpOff" };
    abShutSeqLen = hasAfterburner ? 2 : 0;
    memset(abShutSeq, 0, sizeof(abShutSeq));
    memset(abShutDelayMs, 0, sizeof(abShutDelayMs));
    memset(abShutIgnitionTarget, 0, sizeof(abShutIgnitionTarget));
    clearSeqSideActions(abShutEnterActions);
    clearSeqSideActions(abShutExitActions);
    clearCustomBlocks();
    for (int i = 0; i < abShutSeqLen; i++)
        strncpy(abShutSeq[i], defAbShut[i], sizeof(abShutSeq[i]) - 1);
}

// ── toJson ────────────────────────────────────────────────────
static constexpr const char* WIFI_PASSWORD_RETAINED = "__KEEP_PASSWORD__";

size_t HardwareConfig::toJson(char* buf, size_t len, bool redactPassword) {
    JsonDocument doc;
    toJson(doc, redactPassword);
    return serializeJson(doc, buf, len);
}

void HardwareConfig::toJson(JsonDocument& doc, bool redactPassword) {
    _toDoc(doc);
    if (redactPassword)
        doc["wifi_password"] = wifiPassword[0] ? WIFI_PASSWORD_RETAINED : "";
}

// ── fromJson ──────────────────────────────────────────────────
bool HardwareConfig::validateJson(const char* json, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return false;
    return validateJson(doc);
}

bool HardwareConfig::validateJson(const JsonDocument& doc) {
    if (!requiredStringFits(doc["profile_id"], sizeof(HardwareConfig::profileId))) return false;
    if (!optionalStringFits(doc["profile_desc"], sizeof(HardwareConfig::profileDesc))) return false;
    if (!optionalStringFits(doc["wifi_password"], sizeof(HardwareConfig::wifiPassword))) return false;
    const char* password = doc["wifi_password"] | "";
    if (strcmp(password, WIFI_PASSWORD_RETAINED) != 0 && password[0]) {
        size_t pwLen = strlen(password);
        if (pwLen < 8 || pwLen >= sizeof(HardwareConfig::wifiPassword)) return false;
    }
    if (!validateDisplayLabels(doc["labels"])) return false;
    if (!validateCustomBlockStrings(doc["custom_blocks"])) return false;
    ChannelRegistry* registryForValidation = nullptr;
    ChannelRegistry registry;
    if (!doc["channel_registry"].isNull()) {
        if ((doc["channel_registry"]["version"] | 0) > CHANNEL_REGISTRY_VERSION) return false;
        if (!registry.fromJson(doc["channel_registry"].as<JsonObjectConst>())) return false;
        registryForValidation = &registry;
    }
    if (!validateOilLoops(doc["oil_loops"], registryForValidation)) return false;
    if (!validateHardwareDependencies(doc, registryForValidation)) return false;
    auto sensors = doc["sensors"];
    auto n1 = sensors["n1_rpm"];
    if (n1["enabled"].as<bool>()) {
        if (n1["ppr"].isNull() || n1["ppr"].as<float>() <= 0.0f) return false;
    }
    auto n2 = sensors["n2_rpm"];
    if (doc["has_two_shaft"].as<bool>() && n2["enabled"].as<bool>()) {
        if (n2["ppr"].isNull() || n2["ppr"].as<float>() <= 0.0f) return false;
    }
    if (!validatePlatformPins(doc)) return false;
    return true;
}

bool HardwareConfig::fromJson(const char* json, size_t len) {
    if (!validateJson(json, len)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    _fromDoc(doc);
    return true;
}

// ── _toDoc ────────────────────────────────────────────────────
void HardwareConfig::_toDoc(JsonDocument& doc) {
#ifdef OT_PLATFORM_ESP32S3
    doc["platform"]         = "esp32s3";
#else
    doc["platform"]         = "esp32";
#endif
    doc["profile_id"]       = profileId;
    doc["profile_desc"]     = profileDesc;
    doc["wifi_password"]    = wifiPassword;
    doc["wifi_tx_power_dbm"] = wifiTxPowerDbm;
    doc["has_afterburner"]  = hasAfterburner;
    doc["has_two_shaft"]    = hasTwoShaft;

    auto ctrl = doc["controls"].to<JsonObject>();
    ctrl["stop_pin"]      = stopPin;
    ctrl["stop_active_h"] = stopActiveH;
    ctrl["stop_pullup"]   = stopPullup;
    ctrl["start_pin"]     = startPin;
    ctrl["start_active_h"]= startActiveH;
    ctrl["start_pullup"]  = startPullup;

    auto sensors = doc["sensors"].to<JsonObject>();

    auto n1 = sensors["n1_rpm"].to<JsonObject>();
    n1["enabled"] = hasN1Rpm; n1["pin"] = n1RpmPin; n1["ppr"] = n1RpmPpr;

    auto n2 = sensors["n2_rpm"].to<JsonObject>();
    n2["enabled"] = hasN2Rpm; n2["pin"] = n2RpmPin; n2["ppr"] = n2RpmPpr;

    auto tot = sensors["tot"].to<JsonObject>();
    tot["enabled"] = hasTot; tot["chip"] = totChip; tot["tc_type"] = totTcType;
    tot["clk"] = totClk; tot["cs"] = totCs; tot["miso"] = totMiso; tot["mosi"] = totMosi;

    auto tit = sensors["tit"].to<JsonObject>();
    tit["enabled"] = hasTit; tit["chip"] = titChip; tit["tc_type"] = titTcType;
    tit["clk"] = titClk; tit["cs"] = titCs; tit["miso"] = titMiso; tit["mosi"] = titMosi;

    auto oil = sensors["oil_press"].to<JsonObject>();
    oil["enabled"] = hasOilPress; oil["pin"] = oilPressPin;

    auto fl = sensors["flame"].to<JsonObject>();
    fl["enabled"] = hasFlame; fl["pin"] = flamePin;

    auto ff = sensors["fuel_flow"].to<JsonObject>();
    ff["enabled"] = hasFuelFlow; ff["pin"] = fuelFlowPin;
    ff["type"] = fuelFlowType; ff["pulses_per_litre"] = fuelFlowPulsesPerLitre;

    auto fpress = sensors["fuel_press"].to<JsonObject>();
    fpress["enabled"] = hasFuelPress; fpress["pin"] = fuelPressPin;

    auto p1 = sensors["p1"].to<JsonObject>();
    p1["enabled"] = hasP1; p1["pin"] = p1Pin;

    auto p2 = sensors["p2"].to<JsonObject>();
    p2["enabled"] = hasP2; p2["pin"] = p2Pin;

    auto thi = sensors["throttle_input"].to<JsonObject>();
    thi["enabled"] = hasThrottleInput; thi["pin"] = throttleInputPin; thi["rc_pwm"] = throttleInputRcPwm;

    auto idi = sensors["idle_input"].to<JsonObject>();
    idi["enabled"] = hasIdleInput; idi["pin"] = idleInputPin; idi["rc_pwm"] = idleInputRcPwm;

    auto oilt = sensors["oil_temp"].to<JsonObject>();
    oilt["enabled"] = hasOilTemp; oilt["chip"] = oilTempChip;
    oilt["pin"] = oilTempPin; oilt["clk"] = oilTempPin; oilt["cs"] = oilTempCs;
    oilt["miso"] = oilTempMiso; oilt["mosi"] = oilTempMosi;
    oilt["tc_type"] = oilTempTcType;
    oilt["resolution"]  = oilTempResolution;
    oilt["ntc_beta"]    = ntcBeta;
    oilt["ntc_r0"]      = ntcR0;
    oilt["ntc_r_fixed"] = ntcRFixed;
    oilt["use_raw_poly"] = oilTempUseRawPoly;
    oilt["poly_a"] = oilTempPolyA; oilt["poly_b"] = oilTempPolyB;
    oilt["poly_c"] = oilTempPolyC; oilt["poly_d"] = oilTempPolyD;
    oilt["poly_x_min"] = oilTempPolyXMin; oilt["poly_x_max"] = oilTempPolyXMax;

    auto bvs = sensors["batt_voltage"].to<JsonObject>();
    bvs["enabled"] = hasBattVoltage; bvs["pin"] = battVoltPin;
    bvs["divider"] = battVoltDivider;

    auto torqs = sensors["torque"].to<JsonObject>();
    torqs["enabled"] = hasTorque; torqs["pin"] = torquePin;
    torqs["scale"] = torqueScale; torqs["offset"] = torqueOffset;
    torqs["hx711"] = torqueHx711; torqs["dt_pin"] = torqueDtPin;
    torqs["clk_pin"] = torqueClkPin; torqs["hx_scale"] = torqueHxScale;
    torqs["hx_zero"] = torqueHxZero;

    auto acts = doc["actuators"].to<JsonObject>();

    auto thr = acts["throttle"].to<JsonObject>();
    thr["enabled"]   = hasThrottle; thr["pin"] = throttlePin;
    thr["type"]      = throttleType;
    thr["min_us"]    = throttleMinUs; thr["max_us"] = throttleMaxUs;
    thr["inverted"]  = throttleInverted;
    thr["active_h"]   = throttleActiveH;
    thr["ledc_freq"] = throttleLedcFreqHz; thr["ledc_bits"] = throttleLedcBits;
    thr["pwm_min_pct"] = throttlePwmMinPct; thr["pwm_max_pct"] = throttlePwmMaxPct;

    auto str = acts["starter"].to<JsonObject>();
    str["enabled"]   = hasStarter; str["pin"] = starterPin;
    str["type"]      = starterType;
    str["min_us"]    = starterMinUs; str["max_us"] = starterMaxUs;
    str["inverted"]         = starterInverted;
    str["active_h"]         = starterActiveH;
    str["ledc_freq"]        = starterLedcFreqHz; str["ledc_bits"] = starterLedcBits;
    str["pwm_min_pct"]      = starterPwmMinPct; str["pwm_max_pct"] = starterPwmMaxPct;
    str["assist_enabled"]   = starterAssistEnabled;

    auto oilp = acts["oil_pump"].to<JsonObject>();
    oilp["enabled"] = hasOilPump; oilp["pin"] = oilPumpPin;
    oilp["type"] = oilPumpType; oilp["active_h"] = oilPumpActiveH;
    oilp["min_us"] = oilPumpMinUs; oilp["max_us"] = oilPumpMaxUs;
    oilp["freq_hz"] = oilPumpFreqHz; oilp["res_bits"] = oilPumpResBits;
    oilp["pwm_min_pct"] = oilPumpPwmMinPct; oilp["pwm_max_pct"] = oilPumpPwmMaxPct;
    oilp["has_current"]     = hasOilPumpCurrentSensor;
    oilp["current_pin"]     = oilPumpCurrentPin;
    oilp["current_mv_a"]    = oilPumpCurrentMvPerA;
    oilp["current_zero_v"]  = oilPumpCurrentZeroV;
    oilp["current_max_a"]   = oilPumpCurrentMaxAmps;

    auto fsol = acts["fuel_sol"].to<JsonObject>();
    fsol["enabled"] = hasFuelSol; fsol["pin"] = fuelSolPin; fsol["active_h"] = fuelSolActiveH;

    auto ign = acts["igniter"].to<JsonObject>();
    ign["enabled"] = hasIgniter; ign["pin"] = igniterPin; ign["active_h"] = igniterActiveH;
    ign["pwm"] = igniterPwm; ign["dwell_ms"] = igniterDwellMs; ign["rest_ms"] = igniterRestMs;
    ign["coil"]            = igniterCoil;
    ign["coil_sat_a"]      = igniterCoilSatAmps;
    ign["current_pin"]     = igniterCurrentPin;
    ign["current_mv_a"]    = igniterCurrentMvPerA;
    ign["current_zero_v"]  = igniterCurrentZeroV;
    ign["has_current"]     = hasIgniterCurrentSensor;

    auto ign2 = acts["igniter2"].to<JsonObject>();
    ign2["enabled"] = hasIgniter2; ign2["pin"] = igniter2Pin; ign2["active_h"] = igniter2ActiveH;
    ign2["pwm"] = igniter2Pwm; ign2["dwell_ms"] = igniter2DwellMs; ign2["rest_ms"] = igniter2RestMs;
    ign2["coil"]            = igniter2Coil;
    ign2["coil_sat_a"]      = igniter2CoilSatAmps;
    ign2["current_pin"]     = igniter2CurrentPin;
    ign2["current_mv_a"]    = igniter2CurrentMvPerA;
    ign2["current_zero_v"]  = igniter2CurrentZeroV;
    ign2["has_current"]     = hasIgniter2CurrentSensor;

    auto sen = acts["starter_en"].to<JsonObject>();
    sen["enabled"] = hasStarterEn; sen["pin"] = starterEnPin; sen["active_h"] = starterEnActiveH;
    sen["delay_ms"] = starterEnDelayMs;

    auto abs = acts["ab_sol"].to<JsonObject>();
    abs["enabled"] = hasAbSol; abs["pin"] = abSolPin; abs["active_h"] = abSolActiveH;

    auto airs = acts["airstarter_sol"].to<JsonObject>();
    airs["enabled"] = hasAirstarterSol; airs["pin"] = airstarterSolPin;
    airs["active_h"] = airstarterSolActiveH;

    auto fan = acts["cool_fan"].to<JsonObject>();
    fan["enabled"] = hasCoolFan; fan["pin"] = coolFanPin;
    fan["type"] = coolFanType; fan["active_h"] = coolFanActiveH;
    fan["min_us"] = coolFanMinUs; fan["max_us"] = coolFanMaxUs;
    fan["freq_hz"] = coolFanFreqHz; fan["res_bits"] = coolFanResBits;
    fan["pwm_min_pct"] = coolFanPwmMinPct; fan["pwm_max_pct"] = coolFanPwmMaxPct;

    auto abp = acts["ab_pump"].to<JsonObject>();
    abp["enabled"] = hasAbPump; abp["pin"] = abPumpPin;
    abp["type"] = abPumpType; abp["active_h"] = abPumpActiveH;
    abp["min_us"] = abPumpMinUs; abp["max_us"] = abPumpMaxUs;
    abp["freq_hz"] = abPumpFreqHz; abp["res_bits"] = abPumpResBits;
    abp["pwm_min_pct"] = abPumpPwmMinPct; abp["pwm_max_pct"] = abPumpPwmMaxPct;

    auto scav = acts["oil_scavenge_pump"].to<JsonObject>();
    scav["enabled"]   = hasOilScavengePump;
    scav["pin"]       = oilScavPumpPin;
    scav["type"]      = oilScavPumpType;
    scav["min_us"]    = oilScavPumpMinUs;
    scav["max_us"]    = oilScavPumpMaxUs;
    scav["active_h"]  = oilScavPumpActiveH;
    scav["freq_hz"]   = oilScavPumpFreqHz;
    scav["res_bits"]  = oilScavPumpResBits;
    scav["pwm_min_pct"] = oilScavPumpPwmMinPct;
    scav["pwm_max_pct"] = oilScavPumpPwmMaxPct;

    auto fp2 = acts["fuel_pump2"].to<JsonObject>();
    fp2["enabled"]  = hasFuelPump2; fp2["pin"] = fuelPump2Pin;
    fp2["type"]     = fuelPump2Type; fp2["active_h"] = fuelPump2ActiveH;
    fp2["min_us"]   = fuelPump2MinUs; fp2["max_us"] = fuelPump2MaxUs;
    fp2["freq_hz"]  = fuelPump2FreqHz; fp2["res_bits"] = fuelPump2ResBits;
    fp2["pwm_min_pct"] = fuelPump2PwmMinPct; fp2["pwm_max_pct"] = fuelPump2PwmMaxPct;

    auto blv = acts["bleed_valve"].to<JsonObject>();
    blv["enabled"] = hasBleedValve; blv["pin"] = bleedValvePin;
    blv["type"] = bleedValveType; blv["active_h"] = bleedValveActiveH;
    blv["min_us"] = bleedValveMinUs; blv["max_us"] = bleedValveMaxUs;
    blv["freq_hz"] = bleedValveFreqHz; blv["res_bits"] = bleedValveResBits;
    blv["pwm_min_pct"] = bleedValvePwmMinPct; blv["pwm_max_pct"] = bleedValvePwmMaxPct;

    auto pps = acts["prop_pitch"].to<JsonObject>();
    pps["enabled"] = hasPropPitch; pps["pin"] = propPitchPin;
    pps["type"] = propPitchType;
    pps["min_us"] = propPitchMinUs; pps["max_us"] = propPitchMaxUs;
    pps["freq_hz"] = propPitchFreqHz; pps["res_bits"] = propPitchResBits;
    pps["pwm_min_pct"] = propPitchPwmMinPct; pps["pwm_max_pct"] = propPitchPwmMaxPct;
    pps["active_h"] = propPitchActiveH;

    auto glw = acts["glow_plug"].to<JsonObject>();
    glw["enabled"] = hasGlowPlug; glw["pin"] = glowPlugPin;
    glw["type"] = glowPlugType;
    glw["output_type"] = glowPlugOutputType;
    glw["active_h"] = glowPlugActiveH;
    glw["freq_hz"] = glowPlugFreqHz; glw["res_bits"] = glowPlugResBits;
    glw["pwm_min_pct"] = glowPlugPwmMinPct; glw["pwm_max_pct"] = glowPlugPwmMaxPct;
    glw["fuel_pin"] = wetGlowFuelPin;
    glw["fuel_type"] = wetGlowFuelType;
    glw["fuel_active_h"] = wetGlowFuelActiveH;
    glw["fuel_min_us"] = wetGlowFuelMinUs;
    glw["fuel_max_us"] = wetGlowFuelMaxUs;
    glw["fuel_freq_hz"] = wetGlowFuelFreqHz;
    glw["fuel_res_bits"] = wetGlowFuelResBits;
    glw["fuel_pwm_min_pct"] = wetGlowFuelPwmMinPct;
    glw["fuel_pwm_max_pct"] = wetGlowFuelPwmMaxPct;
    glw["fuel_demand_pct"] = wetGlowFuelDemandPct;
    glw["fuel_delay_ms"] = wetGlowFuelDelayMs;
    glw["current_pin"]    = glowCurrentPin;
    glw["current_mv_a"]   = glowCurrentMvPerA;
    glw["current_zero_v"] = glowCurrentZeroV;
    glw["current_ready_a"]= glowCurrentReadyAmps;
    glw["has_current"]    = hasGlowCurrentSensor;

    auto led = acts["status_led"].to<JsonObject>();
    led["enabled"] = hasStatusLed; led["pin"] = statusLedPin; led["type"] = statusLedType;
    led["mode"] = statusLedMode;
    led["standby_color"] = statusLedStandbyColor;
    led["startup_color"] = statusLedStartupColor;
    led["running_color"] = statusLedRunningColor;
    led["shutdown_color"] = statusLedShutdownColor;
    led["blink_color"] = statusLedBlinkColor;

    auto clus = doc["cluster_serial"].to<JsonObject>();
    clus["enabled"] = hasClusterSerial; clus["tx_pin"] = clusterTxPin;
    clus["rx_pin"] = clusterRxPin;
    clus["baud"] = clusterBaud; clus["interval_ms"] = clusterIntervalMs;

    auto buz = doc["buzzer"].to<JsonObject>();
    buz["enabled"] = hasBuzzer; buz["pin"] = buzzerPin;

    auto mvl = doc["mavlink"].to<JsonObject>();
    mvl["enabled"] = hasMAVLink; mvl["tx_pin"] = mavlinkTxPin;
    mvl["baud"] = mavlinkBaud; mvl["interval_ms"] = mavlinkIntervalMs;

    auto contrl = doc["controllers"].to<JsonObject>();
    contrl["oil_loop"]      = hasOilLoop;
    contrl["throttle_slew"] = hasThrottleSlew;
    contrl["dynamic_idle"]  = hasDynamicIdle;
    contrl["governor"]      = hasGovernor;
    auto loops = doc["oil_loops"].to<JsonArray>();
    for (uint8_t i = 0; i < oilLoopCount; i++) {
        const auto& l = oilLoops[i];
        auto o = loops.add<JsonObject>();
        o["id"] = l.id;
        o["enabled"] = l.enabled;
        o["pressure_input"] = l.pressureInputIndex < channelRegistry.inputCount
            ? channelRegistry.inputs[l.pressureInputIndex].id : "";
        o["pump_output"] = l.pumpOutputIndex < channelRegistry.outputCount
            ? channelRegistry.outputs[l.pumpOutputIndex].id : "";
        o["target_bar"] = l.targetCentiBar / 100.0f;
        o["deadband_bar"] = l.deadbandCentiBar / 100.0f;
        o["min_demand"] = l.minDemandPct / 100.0f;
        o["max_demand"] = l.maxDemandPct / 100.0f;
    }

    auto saf = doc["safety"].to<JsonObject>();
    saf["overspeed"]  = safetyOverspeed;
    saf["overtemp"]   = safetyOvertemp;
    saf["low_oil"]    = safetyLowOil;
    saf["oil_zero"]   = safetyOilZero;
    saf["flameout"]   = safetyFlameout;
    saf["hot_start"]      = safetyHotStart;
    saf["tit_overtemp"]   = safetyTitOvertemp;
    saf["oil_temp_high"]  = safetyOilTempHigh;
    saf["fuel_press_low"] = safetyFuelPressLow;
    saf["batt_low"]       = safetyBattLow;
    saf["surge"]          = safetySurge;

    auto ss = doc["startup_seq"].to<JsonArray>();
    for (int i = 0; i < startupSeqLen; i++) ss.add(startupSeq[i]);
    auto ssd = doc["startup_delay_ms"].to<JsonArray>();
    for (int i = 0; i < startupSeqLen; i++) ssd.add(startupDelayMs[i]);
    auto ssit = doc["startup_ignition_target"].to<JsonArray>();
    for (int i = 0; i < startupSeqLen; i++) ssit.add(startupIgnitionTarget[i]);
    writeSeqSideActions(doc, "startup_enter_actions", startupSeqLen, startupEnterActions);
    writeSeqSideActions(doc, "startup_exit_actions", startupSeqLen, startupExitActions);

    auto ds = doc["shutdown_seq"].to<JsonArray>();
    for (int i = 0; i < shutdownSeqLen; i++) ds.add(shutdownSeq[i]);
    auto dsd = doc["shutdown_delay_ms"].to<JsonArray>();
    for (int i = 0; i < shutdownSeqLen; i++) dsd.add(shutdownDelayMs[i]);
    auto dsit = doc["shutdown_ignition_target"].to<JsonArray>();
    for (int i = 0; i < shutdownSeqLen; i++) dsit.add(shutdownIgnitionTarget[i]);
    writeSeqSideActions(doc, "shutdown_enter_actions", shutdownSeqLen, shutdownEnterActions);
    writeSeqSideActions(doc, "shutdown_exit_actions", shutdownSeqLen, shutdownExitActions);

    auto abt = doc["ab_trigger"].to<JsonObject>();
    abt["source"]           = abTriggerSource;
    abt["requires_arm"]     = abRequiresArmSwitch;
    abt["arm_pin"]          = abArmSwitchPin;
    abt["arm_active_h"]     = abArmSwitchActiveH;
    abt["switch_pin"]       = abSwitchPin;
    abt["switch_active_h"]  = abSwitchActiveH;
    abt["input_pin"]        = abInputPin;
    abt["input_rc_pwm"]     = abInputRcPwm;
    abt["input_min_us"]     = abInputMinUs;
    abt["input_max_us"]     = abInputMaxUs;
    abt["input_threshold"]  = abInputThreshold;

    auto abfl = doc["ab_flame"].to<JsonObject>();
    abfl["enabled"]   = hasAbFlame;
    abfl["pin"]       = abFlamePin;
    abfl["threshold"] = abFlameThreshold;

    auto as = doc["ab_seq"].to<JsonArray>();
    for (int i = 0; i < abSeqLen; i++) as.add(abSeq[i]);
    auto asd = doc["ab_delay_ms"].to<JsonArray>();
    for (int i = 0; i < abSeqLen; i++) asd.add(abDelayMs[i]);
    auto asit = doc["ab_ignition_target"].to<JsonArray>();
    for (int i = 0; i < abSeqLen; i++) asit.add(abIgnitionTarget[i]);
    writeSeqSideActions(doc, "ab_enter_actions", abSeqLen, abEnterActions);
    writeSeqSideActions(doc, "ab_exit_actions", abSeqLen, abExitActions);

    auto ass = doc["ab_shut_seq"].to<JsonArray>();
    for (int i = 0; i < abShutSeqLen; i++) ass.add(abShutSeq[i]);
    auto assd = doc["ab_shut_delay_ms"].to<JsonArray>();
    for (int i = 0; i < abShutSeqLen; i++) assd.add(abShutDelayMs[i]);
    auto assit = doc["ab_shut_ignition_target"].to<JsonArray>();
    for (int i = 0; i < abShutSeqLen; i++) assit.add(abShutIgnitionTarget[i]);
    writeSeqSideActions(doc, "ab_shut_enter_actions", abShutSeqLen, abShutEnterActions);
    writeSeqSideActions(doc, "ab_shut_exit_actions", abShutSeqLen, abShutExitActions);
    writeCustomBlocks(doc);

    auto lbl = doc["labels"].to<JsonObject>();
    lbl["tot"]        = labelTot;
    lbl["tit"]        = labelTit;
    lbl["n1"]         = labelN1;
    lbl["n2"]         = labelN2;
    lbl["oil_press"]  = labelOilPress;
    lbl["oil_temp"]   = labelOilTemp;
    lbl["p1"]         = labelP1;
    lbl["p2"]         = labelP2;
    lbl["fuel_press"] = labelFuelPress;
    lbl["fuel_flow"]  = labelFuelFlow;
    lbl["stop"]       = labelStop;
    lbl["start"]      = labelStart;
    lbl["ab_arm"]     = labelAbArm;

    auto diArr = doc["di_channels"].to<JsonArray>();
    for (int i = 0; i < MAX_DI; i++) {
        auto ch = diArr.add<JsonObject>();
        ch["pin"]          = diCh[i].pin;
        ch["active_h"]     = diCh[i].activeH;
        ch["debounce_ms"]  = diCh[i].debounceMs;
        ch["label"]        = diCh[i].label;
        ch["role"]         = diCh[i].role;
        ch["fault_code"]   = diCh[i].faultCode;
        ch["fault_msg"]    = diCh[i].faultMsg;
        // Only 5 SysMode bits exist — never serialize a value validators
        // would flag (a raw 0xFF here used to brick the next boot).
        ch["active_modes"] = (uint8_t)(diCh[i].activeModes & 0x1F);
    }
    auto registry = doc["channel_registry"].to<JsonObject>();
    registry["version"] = CHANNEL_REGISTRY_VERSION;
    channelRegistry.toJson(registry);
}

// ── _fromDoc ─────────────────────────────────────────────────
void HardwareConfig::_fromDoc(const JsonDocument& doc) {
    // Legacy configurations have no registry.  Their singleton fields remain
    // the boot-time compatibility source until the registry migration is
    // complete; an empty inventory is deliberately not interpreted as an
    // instruction to remove legacy hardware.
    if (!doc["channel_registry"].isNull())
        channelRegistry.fromJson(doc["channel_registry"].as<JsonObjectConst>());
    else
        channelRegistry.clear();
    const char* id   = doc["profile_id"]    | profileId;
    const char* desc = doc["profile_desc"]  | profileDesc;
    const char* pwd  = doc["wifi_password"] | (const char*)wifiPassword;
    if (strcmp(pwd, WIFI_PASSWORD_RETAINED) == 0) pwd = wifiPassword;
    strncpy(profileId,    id,   sizeof(profileId)    - 1);
    profileId[sizeof(profileId) - 1] = '\0';
    strncpy(profileDesc,  desc, sizeof(profileDesc)  - 1);
    profileDesc[sizeof(profileDesc) - 1] = '\0';
    strncpy(wifiPassword, pwd,  sizeof(wifiPassword) - 1);
    wifiPassword[sizeof(wifiPassword) - 1] = '\0';
    wifiTxPowerDbm = constrain(doc["wifi_tx_power_dbm"] | wifiTxPowerDbm, 2, 20);
    if (wifiPassword[0] && strlen(wifiPassword) < 8) {
        Serial.println("[HWCfg] Invalid WiFi password length; using open access point");
        wifiPassword[0] = '\0';
    }
    if (!doc["has_afterburner"].isNull()) hasAfterburner = doc["has_afterburner"].as<bool>();
    if (!doc["has_two_shaft"].isNull())   hasTwoShaft    = doc["has_two_shaft"].as<bool>();

    auto ctrl = doc["controls"];
    stopPin  = ctrl["stop_pin"]  | stopPin;
    if (!ctrl["stop_active_h"].isNull())  stopActiveH  = ctrl["stop_active_h"].as<bool>();
    if (!ctrl["stop_pullup"].isNull())    stopPullup   = ctrl["stop_pullup"].as<bool>();
    startPin = ctrl["start_pin"] | startPin;
    if (!ctrl["start_active_h"].isNull()) startActiveH = ctrl["start_active_h"].as<bool>();
    if (!ctrl["start_pullup"].isNull())   startPullup  = ctrl["start_pullup"].as<bool>();

    auto s = doc["sensors"];

    auto n1 = s["n1_rpm"];
    if (!n1["enabled"].isNull()) hasN1Rpm = n1["enabled"].as<bool>();
    n1RpmPin = n1["pin"] | n1RpmPin;
    n1RpmPpr = n1["ppr"] | n1RpmPpr;

    auto n2 = s["n2_rpm"];
    if (!n2["enabled"].isNull()) hasN2Rpm = n2["enabled"].as<bool>();
    hasN2Rpm = hasTwoShaft && hasN2Rpm;
    n2RpmPin = n2["pin"] | n2RpmPin;
    n2RpmPpr = n2["ppr"] | n2RpmPpr;

    auto tot = s["tot"];
    if (!tot["enabled"].isNull()) hasTot = tot["enabled"].as<bool>();
    { const char* v = tot["chip"]    | totChip;   strncpy(totChip,   v, sizeof(totChip)   - 1); totChip[sizeof(totChip) - 1] = '\0'; }
    { const char* v = tot["tc_type"] | totTcType; strncpy(totTcType, v, sizeof(totTcType) - 1); totTcType[sizeof(totTcType) - 1] = '\0'; }
    totClk  = tot["clk"]  | totClk;
    totCs   = tot["cs"]   | totCs;
    totMiso = tot["miso"] | totMiso;
    totMosi = tot["mosi"] | totMosi;

    auto tit = s["tit"];
    if (!tit["enabled"].isNull()) hasTit = tit["enabled"].as<bool>();
    { const char* v = tit["chip"]    | titChip;   strncpy(titChip,   v, sizeof(titChip)   - 1); titChip[sizeof(titChip) - 1] = '\0'; }
    { const char* v = tit["tc_type"] | titTcType; strncpy(titTcType, v, sizeof(titTcType) - 1); titTcType[sizeof(titTcType) - 1] = '\0'; }
    titClk  = tit["clk"]  | titClk;
    titCs   = tit["cs"]   | titCs;
    titMiso = tit["miso"] | titMiso;
    titMosi = tit["mosi"] | titMosi;

    auto oilp = s["oil_press"];
    if (!oilp["enabled"].isNull()) hasOilPress = oilp["enabled"].as<bool>();
    oilPressPin = oilp["pin"] | oilPressPin;

    auto fl = s["flame"];
    if (!fl["enabled"].isNull()) hasFlame = fl["enabled"].as<bool>();
    flamePin = fl["pin"] | flamePin;

    auto ff = s["fuel_flow"];
    if (!ff["enabled"].isNull()) hasFuelFlow = ff["enabled"].as<bool>();
    fuelFlowPin              = ff["pin"]              | fuelFlowPin;
    fuelFlowType             = ff["type"]             | fuelFlowType;
    fuelFlowPulsesPerLitre   = ff["pulses_per_litre"] | fuelFlowPulsesPerLitre;

    auto fpress = s["fuel_press"];
    if (!fpress["enabled"].isNull()) hasFuelPress = fpress["enabled"].as<bool>();
    fuelPressPin = fpress["pin"] | fuelPressPin;

    auto p1 = s["p1"];
    if (!p1["enabled"].isNull()) hasP1 = p1["enabled"].as<bool>();
    p1Pin = p1["pin"] | p1Pin;

    auto p2 = s["p2"];
    if (!p2["enabled"].isNull()) hasP2 = p2["enabled"].as<bool>();
    p2Pin = p2["pin"] | p2Pin;

    auto thi = s["throttle_input"];
    if (!thi["enabled"].isNull()) hasThrottleInput = thi["enabled"].as<bool>();
    throttleInputPin   = thi["pin"]    | throttleInputPin;
    if (!thi["rc_pwm"].isNull()) throttleInputRcPwm = thi["rc_pwm"].as<bool>();

    auto idi = s["idle_input"];
    if (!idi["enabled"].isNull()) hasIdleInput = idi["enabled"].as<bool>();
    idleInputPin       = idi["pin"]    | idleInputPin;
    if (!idi["rc_pwm"].isNull()) idleInputRcPwm = idi["rc_pwm"].as<bool>();

    auto oilt = s["oil_temp"];
    if (!oilt["enabled"].isNull()) hasOilTemp = oilt["enabled"].as<bool>();
    { const char* v = oilt["chip"] | oilTempChip; strncpy(oilTempChip, v, sizeof(oilTempChip) - 1); oilTempChip[sizeof(oilTempChip) - 1] = '\0'; }
    if (strcmp(oilTempChip, "ntc") == 0 || strcmp(oilTempChip, "ds18b20") == 0)
        oilTempPin = oilt["pin"] | oilTempPin;
    else
        oilTempPin = oilt["clk"] | oilTempPin;
    oilTempCs   = oilt["cs"]   | oilTempCs;
    oilTempMiso = oilt["miso"] | oilTempMiso;
    oilTempMosi = oilt["mosi"] | oilTempMosi;
    { const char* v = oilt["tc_type"] | oilTempTcType; strncpy(oilTempTcType, v, sizeof(oilTempTcType) - 1); oilTempTcType[sizeof(oilTempTcType) - 1] = '\0'; }
    oilTempResolution = oilt["resolution"] | oilTempResolution;
    ntcBeta   = oilt["ntc_beta"]    | ntcBeta;
    ntcR0     = oilt["ntc_r0"]      | ntcR0;
    ntcRFixed = oilt["ntc_r_fixed"] | ntcRFixed;
    oilTempUseRawPoly = oilt["use_raw_poly"] | oilTempUseRawPoly;
    oilTempPolyA = oilt["poly_a"] | oilTempPolyA;
    oilTempPolyB = oilt["poly_b"] | oilTempPolyB;
    oilTempPolyC = oilt["poly_c"] | oilTempPolyC;
    oilTempPolyD = oilt["poly_d"] | oilTempPolyD;
    oilTempPolyXMin = oilt["poly_x_min"] | oilTempPolyXMin;
    oilTempPolyXMax = oilt["poly_x_max"] | oilTempPolyXMax;

    auto bvs = s["batt_voltage"];
    if (!bvs["enabled"].isNull()) hasBattVoltage = bvs["enabled"].as<bool>();
    battVoltPin     = bvs["pin"]     | battVoltPin;
    battVoltDivider = bvs["divider"] | battVoltDivider;

    auto torqs = s["torque"];
    if (!torqs["enabled"].isNull()) hasTorque = torqs["enabled"].as<bool>();
    torquePin    = torqs["pin"]    | torquePin;
    torqueScale  = torqs["scale"]  | torqueScale;
    torqueOffset = torqs["offset"] | torqueOffset;
    if (!torqs["hx711"].isNull()) torqueHx711 = torqs["hx711"].as<bool>();
    torqueDtPin   = torqs["dt_pin"]  | torqueDtPin;
    torqueClkPin  = torqs["clk_pin"] | torqueClkPin;
    torqueHxScale = torqs["hx_scale"] | torqueHxScale;
    torqueHxZero  = torqs["hx_zero"] | torqueHxZero;

    auto a = doc["actuators"];

    auto thr = a["throttle"];
    if (!thr["enabled"].isNull())  hasThrottle      = thr["enabled"].as<bool>();
    throttlePin        = thr["pin"]       | throttlePin;
    throttleType       = thr["type"]      | throttleType;
    throttleMinUs      = thr["min_us"]    | throttleMinUs;
    throttleMaxUs      = thr["max_us"]    | throttleMaxUs;
    if (!thr["inverted"].isNull()) throttleInverted  = thr["inverted"].as<bool>();
    if (!thr["active_h"].isNull())  throttleActiveH   = thr["active_h"].as<bool>();
    throttleLedcFreqHz = thr["ledc_freq"] | throttleLedcFreqHz;
    throttleLedcBits   = thr["ledc_bits"] | throttleLedcBits;
    throttlePwmMinPct  = thr["pwm_min_pct"] | throttlePwmMinPct;
    throttlePwmMaxPct  = thr["pwm_max_pct"] | throttlePwmMaxPct;

    auto str = a["starter"];
    if (!str["enabled"].isNull())  hasStarter        = str["enabled"].as<bool>();
    starterPin         = str["pin"]       | starterPin;
    starterType        = str["type"]      | starterType;
    if (!str["inverted"].isNull()) starterInverted    = str["inverted"].as<bool>();
    if (!str["active_h"].isNull())  starterActiveH     = str["active_h"].as<bool>();
    starterLedcFreqHz  = str["ledc_freq"] | starterLedcFreqHz;
    starterLedcBits    = str["ledc_bits"] | starterLedcBits;
    starterPwmMinPct   = str["pwm_min_pct"] | starterPwmMinPct;
    starterPwmMaxPct   = str["pwm_max_pct"] | starterPwmMaxPct;
    starterMinUs = str["min_us"] | starterMinUs;
    starterMaxUs = str["max_us"] | starterMaxUs;
    if (!str["assist_enabled"].isNull()) starterAssistEnabled = str["assist_enabled"].as<bool>();

    auto op = a["oil_pump"];
    if (!op["enabled"].isNull())  hasOilPump  = op["enabled"].as<bool>();
    oilPumpPin     = op["pin"]      | oilPumpPin;
    oilPumpType    = op["type"]     | oilPumpType;
    if (!op["active_h"].isNull()) oilPumpActiveH = op["active_h"].as<bool>();
    oilPumpMinUs   = op["min_us"]   | oilPumpMinUs;
    oilPumpMaxUs   = op["max_us"]   | oilPumpMaxUs;
    oilPumpFreqHz  = op["freq_hz"]  | oilPumpFreqHz;
    oilPumpResBits = op["res_bits"] | oilPumpResBits;
    oilPumpPwmMinPct = op["pwm_min_pct"] | oilPumpPwmMinPct;
    oilPumpPwmMaxPct = op["pwm_max_pct"] | oilPumpPwmMaxPct;
    if (!op["has_current"].isNull()) hasOilPumpCurrentSensor = hasOilPump && op["has_current"].as<bool>();
    oilPumpCurrentPin     = op["current_pin"]    | oilPumpCurrentPin;
    oilPumpCurrentMvPerA  = op["current_mv_a"]   | oilPumpCurrentMvPerA;
    oilPumpCurrentZeroV   = op["current_zero_v"] | oilPumpCurrentZeroV;
    oilPumpCurrentMaxAmps = op["current_max_a"]  | oilPumpCurrentMaxAmps;

    auto fsol = a["fuel_sol"];
    if (!fsol["enabled"].isNull()) hasFuelSol   = fsol["enabled"].as<bool>();
    fuelSolPin   = fsol["pin"]      | fuelSolPin;
    if (!fsol["active_h"].isNull()) fuelSolActiveH = fsol["active_h"].as<bool>();

    auto ign = a["igniter"];
    if (!ign["enabled"].isNull()) hasIgniter   = ign["enabled"].as<bool>();
    igniterPin   = ign["pin"]      | igniterPin;
    if (!ign["active_h"].isNull()) igniterActiveH = ign["active_h"].as<bool>();
    if (!ign["pwm"].isNull())      igniterPwm     = ign["pwm"].as<bool>();
    igniterDwellMs = ign["dwell_ms"] | igniterDwellMs;
    igniterRestMs  = ign["rest_ms"]  | igniterRestMs;
    if (!ign["coil"].isNull())       igniterCoil           = ign["coil"].as<bool>();
    igniterCoilSatAmps    = ign["coil_sat_a"]     | igniterCoilSatAmps;
    igniterCurrentPin     = ign["current_pin"]    | igniterCurrentPin;
    igniterCurrentMvPerA  = ign["current_mv_a"]   | igniterCurrentMvPerA;
    igniterCurrentZeroV   = ign["current_zero_v"] | igniterCurrentZeroV;
    if (!ign["has_current"].isNull()) hasIgniterCurrentSensor = hasIgniter && ign["has_current"].as<bool>();

    auto ign2 = a["igniter2"];
    if (!ign2["enabled"].isNull()) hasIgniter2    = ign2["enabled"].as<bool>();
    igniter2Pin    = ign2["pin"]      | igniter2Pin;
    if (!ign2["active_h"].isNull()) igniter2ActiveH = ign2["active_h"].as<bool>();
    if (!ign2["pwm"].isNull())      igniter2Pwm     = ign2["pwm"].as<bool>();
    igniter2DwellMs = ign2["dwell_ms"] | igniter2DwellMs;
    igniter2RestMs  = ign2["rest_ms"]  | igniter2RestMs;
    if (!ign2["coil"].isNull())       igniter2Coil           = ign2["coil"].as<bool>();
    igniter2CoilSatAmps    = ign2["coil_sat_a"]     | igniter2CoilSatAmps;
    igniter2CurrentPin     = ign2["current_pin"]    | igniter2CurrentPin;
    igniter2CurrentMvPerA  = ign2["current_mv_a"]   | igniter2CurrentMvPerA;
    igniter2CurrentZeroV   = ign2["current_zero_v"] | igniter2CurrentZeroV;
    if (!ign2["has_current"].isNull()) hasIgniter2CurrentSensor = hasIgniter2 && ign2["has_current"].as<bool>();

    auto sen = a["starter_en"];
    if (!sen["enabled"].isNull()) hasStarterEn   = sen["enabled"].as<bool>();
    starterEnPin       = sen["pin"]       | starterEnPin;
    if (!sen["active_h"].isNull()) starterEnActiveH = sen["active_h"].as<bool>();
    starterEnDelayMs   = sen["delay_ms"]  | starterEnDelayMs;

    auto abs2 = a["ab_sol"];
    if (!abs2["enabled"].isNull()) hasAbSol   = hasAfterburner && abs2["enabled"].as<bool>();
    abSolPin   = abs2["pin"]      | abSolPin;
    if (!abs2["active_h"].isNull()) abSolActiveH = abs2["active_h"].as<bool>();

    auto airs = a["airstarter_sol"];
    if (!airs["enabled"].isNull()) hasAirstarterSol = airs["enabled"].as<bool>();
    airstarterSolPin = airs["pin"] | airstarterSolPin;
    if (!airs["active_h"].isNull()) airstarterSolActiveH = airs["active_h"].as<bool>();

    auto fan = a["cool_fan"];
    if (!fan["enabled"].isNull()) hasCoolFan = fan["enabled"].as<bool>();
    coolFanPin    = fan["pin"]      | coolFanPin;
    coolFanType   = fan["type"]     | coolFanType;
    if (!fan["active_h"].isNull()) coolFanActiveH = fan["active_h"].as<bool>();
    coolFanMinUs  = fan["min_us"]   | coolFanMinUs;
    coolFanMaxUs  = fan["max_us"]   | coolFanMaxUs;
    coolFanFreqHz = fan["freq_hz"]  | coolFanFreqHz;
    coolFanResBits= fan["res_bits"] | coolFanResBits;
    coolFanPwmMinPct = fan["pwm_min_pct"] | coolFanPwmMinPct;
    coolFanPwmMaxPct = fan["pwm_max_pct"] | coolFanPwmMaxPct;

    auto abp = a["ab_pump"];
    if (!abp["enabled"].isNull()) hasAbPump = hasAfterburner && abp["enabled"].as<bool>();
    abPumpPin    = abp["pin"]      | abPumpPin;
    abPumpType   = abp["type"]     | abPumpType;
    if (!abp["active_h"].isNull()) abPumpActiveH = abp["active_h"].as<bool>();
    abPumpMinUs  = abp["min_us"]   | abPumpMinUs;
    abPumpMaxUs  = abp["max_us"]   | abPumpMaxUs;
    abPumpFreqHz = abp["freq_hz"]  | abPumpFreqHz;
    abPumpResBits= abp["res_bits"] | abPumpResBits;
    abPumpPwmMinPct = abp["pwm_min_pct"] | abPumpPwmMinPct;
    abPumpPwmMaxPct = abp["pwm_max_pct"] | abPumpPwmMaxPct;

    auto scav = a["oil_scavenge_pump"];
    if (!scav["enabled"].isNull()) hasOilScavengePump = scav["enabled"].as<bool>();
    oilScavPumpPin     = scav["pin"]      | oilScavPumpPin;
    oilScavPumpType    = scav["type"]     | oilScavPumpType;
    oilScavPumpMinUs   = scav["min_us"]   | oilScavPumpMinUs;
    oilScavPumpMaxUs   = scav["max_us"]   | oilScavPumpMaxUs;
    if (!scav["active_h"].isNull()) oilScavPumpActiveH = scav["active_h"].as<bool>();
    oilScavPumpFreqHz  = scav["freq_hz"]  | oilScavPumpFreqHz;
    oilScavPumpResBits = scav["res_bits"] | oilScavPumpResBits;
    oilScavPumpPwmMinPct = scav["pwm_min_pct"] | oilScavPumpPwmMinPct;
    oilScavPumpPwmMaxPct = scav["pwm_max_pct"] | oilScavPumpPwmMaxPct;

    auto fp2 = a["fuel_pump2"];
    if (!fp2["enabled"].isNull()) hasFuelPump2 = fp2["enabled"].as<bool>();
    fuelPump2Pin     = fp2["pin"]      | fuelPump2Pin;
    fuelPump2Type    = fp2["type"]     | fuelPump2Type;
    if (!fp2["active_h"].isNull()) fuelPump2ActiveH = fp2["active_h"].as<bool>();
    fuelPump2MinUs   = fp2["min_us"]   | fuelPump2MinUs;
    fuelPump2MaxUs   = fp2["max_us"]   | fuelPump2MaxUs;
    fuelPump2FreqHz  = fp2["freq_hz"]  | fuelPump2FreqHz;
    fuelPump2ResBits = fp2["res_bits"] | fuelPump2ResBits;
    fuelPump2PwmMinPct = fp2["pwm_min_pct"] | fuelPump2PwmMinPct;
    fuelPump2PwmMaxPct = fp2["pwm_max_pct"] | fuelPump2PwmMaxPct;

    auto blv = a["bleed_valve"];
    if (!blv["enabled"].isNull()) hasBleedValve  = blv["enabled"].as<bool>();
    bleedValveType   = blv["type"]     | bleedValveType;
    bleedValvePin    = blv["pin"]      | bleedValvePin;
    if (!blv["active_h"].isNull()) bleedValveActiveH = blv["active_h"].as<bool>();
    bleedValveMinUs  = blv["min_us"]   | bleedValveMinUs;
    bleedValveMaxUs  = blv["max_us"]   | bleedValveMaxUs;
    bleedValveFreqHz = blv["freq_hz"]  | bleedValveFreqHz;
    bleedValveResBits= blv["res_bits"] | bleedValveResBits;
    bleedValvePwmMinPct = blv["pwm_min_pct"] | bleedValvePwmMinPct;
    bleedValvePwmMaxPct = blv["pwm_max_pct"] | bleedValvePwmMaxPct;

    auto pps = a["prop_pitch"];
    if (!pps["enabled"].isNull()) hasPropPitch = pps["enabled"].as<bool>();
    propPitchType   = pps["type"]     | propPitchType;
    propPitchPin    = pps["pin"]      | propPitchPin;
    propPitchMinUs  = pps["min_us"]   | propPitchMinUs;
    propPitchMaxUs  = pps["max_us"]   | propPitchMaxUs;
    propPitchFreqHz = pps["freq_hz"]  | propPitchFreqHz;
    propPitchResBits= pps["res_bits"] | propPitchResBits;
    propPitchPwmMinPct = pps["pwm_min_pct"] | propPitchPwmMinPct;
    propPitchPwmMaxPct = pps["pwm_max_pct"] | propPitchPwmMaxPct;
    if (!pps["active_h"].isNull()) propPitchActiveH = pps["active_h"].as<bool>();

    auto glw = a["glow_plug"];
    if (!glw["enabled"].isNull()) hasGlowPlug  = glw["enabled"].as<bool>();
    glowPlugType    = glw["type"]     | glowPlugType;
    glowPlugOutputType = glw["output_type"] | glowPlugOutputType;
    if (!glw["active_h"].isNull()) glowPlugActiveH = glw["active_h"].as<bool>();
    glowPlugPin     = glw["pin"]      | glowPlugPin;
    glowPlugFreqHz  = glw["freq_hz"]  | glowPlugFreqHz;
    glowPlugResBits = glw["res_bits"] | glowPlugResBits;
    glowPlugPwmMinPct = glw["pwm_min_pct"] | glowPlugPwmMinPct;
    glowPlugPwmMaxPct = glw["pwm_max_pct"] | glowPlugPwmMaxPct;
    wetGlowFuelPin       = glw["fuel_pin"]        | wetGlowFuelPin;
    wetGlowFuelType      = glw["fuel_type"]       | wetGlowFuelType;
    if (!glw["fuel_active_h"].isNull()) wetGlowFuelActiveH = glw["fuel_active_h"].as<bool>();
    wetGlowFuelMinUs     = glw["fuel_min_us"]     | wetGlowFuelMinUs;
    wetGlowFuelMaxUs     = glw["fuel_max_us"]     | wetGlowFuelMaxUs;
    wetGlowFuelFreqHz    = glw["fuel_freq_hz"]    | wetGlowFuelFreqHz;
    wetGlowFuelResBits   = glw["fuel_res_bits"]   | wetGlowFuelResBits;
    wetGlowFuelPwmMinPct = glw["fuel_pwm_min_pct"] | wetGlowFuelPwmMinPct;
    wetGlowFuelPwmMaxPct = glw["fuel_pwm_max_pct"] | wetGlowFuelPwmMaxPct;
    wetGlowFuelDemandPct = glw["fuel_demand_pct"] | wetGlowFuelDemandPct;
    wetGlowFuelDelayMs   = glw["fuel_delay_ms"]   | wetGlowFuelDelayMs;
    glowCurrentPin      = glw["current_pin"]     | glowCurrentPin;
    glowCurrentMvPerA   = glw["current_mv_a"]    | glowCurrentMvPerA;
    glowCurrentZeroV    = glw["current_zero_v"]  | glowCurrentZeroV;
    glowCurrentReadyAmps= glw["current_ready_a"] | glowCurrentReadyAmps;
    if (!glw["has_current"].isNull()) hasGlowCurrentSensor = hasGlowPlug && glw["has_current"].as<bool>();
    glowPlugType = constrain(glowPlugType, 0, 2);
    if (glowPlugType == 1) glowPlugType = 0;  // legacy 'current-sensed' retired; current = hasGlowCurrentSensor
    glowPlugOutputType = constrain(glowPlugOutputType, 0, 1);
    wetGlowFuelType = constrain(wetGlowFuelType, 0, 2);
    wetGlowFuelMinUs = constrain(wetGlowFuelMinUs, 500, 2500);
    wetGlowFuelMaxUs = constrain(wetGlowFuelMaxUs, 500, 2500);
    if (wetGlowFuelMaxUs < wetGlowFuelMinUs) {
        int tmp = wetGlowFuelMinUs;
        wetGlowFuelMinUs = wetGlowFuelMaxUs;
        wetGlowFuelMaxUs = tmp;
    }
    wetGlowFuelFreqHz = constrain(wetGlowFuelFreqHz, 1, 100000);
    wetGlowFuelResBits = constrain(wetGlowFuelResBits, 1, 16);
    wetGlowFuelDemandPct = constrain(wetGlowFuelDemandPct, 0.0f, 100.0f);
    if (wetGlowFuelDelayMs < 0) wetGlowFuelDelayMs = 0;

    auto led = a["status_led"];
    const bool ledEnabledPresent = !led["enabled"].isNull();
    const bool ledPinPresent = !led["pin"].isNull();
    const bool ledTypePresent = !led["type"].isNull();
    if (ledEnabledPresent) hasStatusLed = led["enabled"].as<bool>();
    statusLedPin = led["pin"] | statusLedPin;
    statusLedType = led["type"] | statusLedType;
    statusLedMode = led["mode"] | statusLedMode;
    statusLedStandbyColor  = led["standby_color"]  | statusLedStandbyColor;
    statusLedStartupColor  = led["startup_color"]  | statusLedStartupColor;
    statusLedRunningColor  = led["running_color"]  | statusLedRunningColor;
    statusLedShutdownColor = led["shutdown_color"] | statusLedShutdownColor;
    statusLedBlinkColor    = led["blink_color"]    | statusLedBlinkColor;
    if (statusLedMode < 0 || statusLedMode > 1) statusLedMode = DEFAULT_STATUS_LED_MODE;
    statusLedStandbyColor  &= 0xFFFFFFu;
    statusLedStartupColor  &= 0xFFFFFFu;
    statusLedRunningColor  &= 0xFFFFFFu;
    statusLedShutdownColor &= 0xFFFFFFu;
    statusLedBlinkColor    &= 0xFFFFFFu;
    if (statusLedMode == 1) {
        hasStatusLed = true;
        statusLedType = 1;
    }
#if defined(OT_PLATFORM_ESP32S3)
    if (!ledEnabledPresent) hasStatusLed = true;
    if (hasStatusLed && (!ledPinPresent ||
        statusLedPin < 0 ||
        statusLedPin == AUTO_S3_RGB_STATUS_LED_PIN ||
        statusLedPin == 38)) {
        auto moveOldRgbMiso = [](int& pin) {
            if (pin == 38) pin = OT_SPI_MISO_DEFAULT;
        };
        moveOldRgbMiso(totMiso);
        moveOldRgbMiso(titMiso);
        moveOldRgbMiso(oilTempMiso);
        hasStatusLed = true;
        statusLedPin = DEFAULT_STATUS_LED_PIN;
        statusLedType = DEFAULT_STATUS_LED_TYPE;
        statusLedMode = constrain(statusLedMode, 0, 1);
        Serial.println("[HWCfg] Status LED migrated to YD-ESP32-S3 RGB LED default");
    }
    if (hasStatusLed && statusLedPin == DEFAULT_STATUS_LED_PIN && !ledTypePresent) {
        statusLedType = DEFAULT_STATUS_LED_TYPE;
    }
#else
    if (statusLedPin == AUTO_S3_RGB_STATUS_LED_PIN) statusLedPin = DEFAULT_STATUS_LED_PIN;
#endif
    Serial.printf("[HWCfg] Status LED: enabled=%d pin=%d type=%d mode=%d\n",
                  hasStatusLed ? 1 : 0, statusLedPin, statusLedType, statusLedMode);

    auto clus = doc["cluster_serial"];
    if (!clus["enabled"].isNull()) hasClusterSerial = clus["enabled"].as<bool>();
    clusterTxPin     = clus["tx_pin"]     | clusterTxPin;
    clusterRxPin     = clus["rx_pin"]     | clusterRxPin;
    clusterBaud      = clus["baud"]       | clusterBaud;
    clusterIntervalMs= clus["interval_ms"]| clusterIntervalMs;

    auto buz = doc["buzzer"];
    if (!buz["enabled"].isNull()) hasBuzzer = buz["enabled"].as<bool>();
    buzzerPin = buz["pin"] | buzzerPin;

    auto mvl = doc["mavlink"];
    if (!mvl["enabled"].isNull()) hasMAVLink = mvl["enabled"].as<bool>();
    mavlinkTxPin    = mvl["tx_pin"]      | mavlinkTxPin;
    mavlinkBaud     = mvl["baud"]        | mavlinkBaud;
    mavlinkIntervalMs = mvl["interval_ms"] | mavlinkIntervalMs;

    if (channelRegistry.inputCount || channelRegistry.outputCount) {
        auto bound = [](const char* key, ChannelRegistry::Direction dir) -> const ChannelRegistry::Channel* {
            for (uint8_t i = 0; i < HardwareConfig::channelRegistry.bindingCount; i++)
                if (strcmp(HardwareConfig::channelRegistry.bindings[i].key, key) == 0)
                    return HardwareConfig::channelRegistry.find(HardwareConfig::channelRegistry.bindings[i].channelId, dir);
            return nullptr;
        };
        auto byIdOrRole = [](ChannelRegistry::Direction dir, const char* id, const char* role) -> const ChannelRegistry::Channel* {
            if (id) {
                const auto* c = HardwareConfig::channelRegistry.find(id, dir);
                if (c) return c;
            }
            const ChannelRegistry::Channel* list = dir == ChannelRegistry::Input
                ? HardwareConfig::channelRegistry.inputs
                : HardwareConfig::channelRegistry.outputs;
            uint8_t count = dir == ChannelRegistry::Input
                ? HardwareConfig::channelRegistry.inputCount
                : HardwareConfig::channelRegistry.outputCount;
            for (uint8_t i = 0; i < count; i++)
                if (role && strcmp(list[i].role, role) == 0) return &list[i];
            return nullptr;
        };
        auto outputType = [](ChannelRegistry::Driver d) {
            return d == ChannelRegistry::Servo ? 0 : d == ChannelRegistry::Pwm ? 1 : 2;
        };
        auto applyPulse = [](const ChannelRegistry::Channel* c, bool& has, int& pin) {
            if (c && c->pin >= 0 && c->driver == ChannelRegistry::Pulse) { has = true; pin = c->pin; }
        };
        auto applyAnalog = [](const ChannelRegistry::Channel* c, bool& has, int& pin) {
            if (c && c->pin >= 0 && c->driver == ChannelRegistry::Analog) { has = true; pin = c->pin; }
        };
        auto applyInput = [](const ChannelRegistry::Channel* c, bool& has, int& pin, bool& rcPwm) {
            if (!c || c->pin < 0) return;
            if (c->driver == ChannelRegistry::RcPwm || c->driver == ChannelRegistry::Analog) {
                has = true; pin = c->pin; rcPwm = c->driver == ChannelRegistry::RcPwm;
            }
        };
        auto applyOutput = [&](const ChannelRegistry::Channel* c, bool& has, int& pin, int& type) {
            if (!c || c->pin < 0) return;
            has = true; pin = c->pin; type = outputType(c->driver);
        };

        applyPulse(bound("primary_n1", ChannelRegistry::Input), hasN1Rpm, n1RpmPin);
        applyPulse(bound("primary_n2", ChannelRegistry::Input), hasN2Rpm, n2RpmPin);
        if (hasN2Rpm) hasTwoShaft = true;
        applyAnalog(byIdOrRole(ChannelRegistry::Input, "oil_pressure_main", "pressure"), hasOilPress, oilPressPin);
        applyInput(bound("operator_throttle", ChannelRegistry::Input), hasThrottleInput, throttleInputPin, throttleInputRcPwm);
        const auto* mainFuel = bound("main_fuel_output", ChannelRegistry::Output);
        if (!mainFuel) mainFuel = byIdOrRole(ChannelRegistry::Output, "main_fuel", "fuel");
        applyOutput(mainFuel, hasThrottle, throttlePin, throttleType);
        const auto* starter = bound("main_starter", ChannelRegistry::Output);
        if (!starter) starter = byIdOrRole(ChannelRegistry::Output, "starter_main", "starter");
        applyOutput(starter, hasStarter, starterPin, starterType);
        applyOutput(byIdOrRole(ChannelRegistry::Output, "oil_pump_main", "oil_pump"),
                    hasOilPump, oilPumpPin, oilPumpType);
        applyOutput(byIdOrRole(ChannelRegistry::Output, "cooling_fan_main", "cooling_fan"),
                    hasCoolFan, coolFanPin, coolFanType);
        applyOutput(byIdOrRole(ChannelRegistry::Output, "oil_scavenge_main", "scavenge_pump"),
                    hasOilScavengePump, oilScavPumpPin, oilScavPumpType);
        applyOutput(byIdOrRole(ChannelRegistry::Output, "bleed_valve_main", "valve"),
                    hasBleedValve, bleedValvePin, bleedValveType);
        if (const auto* c = bound("main_fuel_shutoff", ChannelRegistry::Output)) {
            if (c->pin >= 0) { hasFuelSol = true; fuelSolPin = c->pin; }
        }
    }

    auto contrl = doc["controllers"];
    if (!contrl["oil_loop"].isNull())      hasOilLoop      = contrl["oil_loop"].as<bool>();
    if (!contrl["throttle_slew"].isNull()) hasThrottleSlew = contrl["throttle_slew"].as<bool>();
    if (!contrl["dynamic_idle"].isNull())  hasDynamicIdle  = contrl["dynamic_idle"].as<bool>();
    if (!contrl["governor"].isNull())      hasGovernor     = contrl["governor"].as<bool>();
    oilLoopCount = 0;
    for (int i = 0; i < MAX_OIL_LOOPS; i++) oilLoops[i] = OilLoopDef{};
    if (doc["oil_loops"].is<JsonArrayConst>()) {
        auto outType = [](ChannelRegistry::Driver d) {
            return d == ChannelRegistry::Servo ? 0 : d == ChannelRegistry::Pwm ? 1 : 2;
        };
        auto inputIndex = [](const char* id) -> uint8_t {
            for (uint8_t i = 0; i < HardwareConfig::channelRegistry.inputCount; i++)
                if (!strcmp(HardwareConfig::channelRegistry.inputs[i].id, id)) return i;
            return 255;
        };
        auto outputIndex = [](const char* id) -> uint8_t {
            for (uint8_t i = 0; i < HardwareConfig::channelRegistry.outputCount; i++)
                if (!strcmp(HardwareConfig::channelRegistry.outputs[i].id, id)) return i;
            return 255;
        };
        bool anyEnabledLoop = false;
        for (JsonObjectConst src : doc["oil_loops"].as<JsonArrayConst>()) {
            if (oilLoopCount >= MAX_OIL_LOOPS) break;
            OilLoopDef& l = oilLoops[oilLoopCount++];
            strlcpy(l.id, src["id"] | "", sizeof(l.id));
            l.pressureInputIndex = inputIndex(src["pressure_input"] | "");
            l.pumpOutputIndex = outputIndex(src["pump_output"] | "");
            l.enabled = src["enabled"] | false;
            l.targetCentiBar = (uint16_t)constrain((int)((src["target_bar"] | 2.5f) * 100.0f), 0, 2000);
            l.deadbandCentiBar = (uint16_t)constrain((int)((src["deadband_bar"] | 0.2f) * 100.0f), 0, 500);
            l.minDemandPct = (uint8_t)constrain((int)((src["min_demand"] | 0.18f) * 100.0f), 0, 100);
            l.maxDemandPct = (uint8_t)constrain((int)((src["max_demand"] | 1.0f) * 100.0f), l.minDemandPct, 100);
            if (l.enabled && !anyEnabledLoop) {
                const auto* pressure = l.pressureInputIndex < channelRegistry.inputCount ? &channelRegistry.inputs[l.pressureInputIndex] : nullptr;
                const auto* pump = l.pumpOutputIndex < channelRegistry.outputCount ? &channelRegistry.outputs[l.pumpOutputIndex] : nullptr;
                if (pressure && pressure->pin >= 0) { hasOilPress = true; oilPressPin = pressure->pin; }
                if (pump && pump->pin >= 0) { hasOilPump = true; oilPumpPin = pump->pin; oilPumpType = outType(pump->driver); }
                anyEnabledLoop = true;
            }
        }
        if (anyEnabledLoop) hasOilLoop = true;
    }
    if (hasOilLoop && (!hasOilPress || !hasOilPump)) {
        Serial.println("[HWCfg] Oil pressure loop disabled: requires oil pressure sensor and oil pump");
        hasOilLoop = false;
    }
    if (hasThrottleSlew && !hasThrottle) {
        Serial.println("[HWCfg] Throttle slew disabled: requires throttle output");
        hasThrottleSlew = false;
    }
    if (hasDynamicIdle && (!hasThrottle || (!hasN1Rpm && !hasN2Rpm))) {
        Serial.println("[HWCfg] Dynamic idle disabled: requires throttle output and an RPM sensor");
        hasDynamicIdle = false;
    }
    const bool hasProportionalPropPitch = hasPropPitch && propPitchType != 2;
    if (hasGovernor && (!hasN2Rpm || (!hasThrottle && !hasProportionalPropPitch))) {
        Serial.println("[HWCfg] Governor disabled: requires N2 RPM and throttle or proportional prop pitch output");
        hasGovernor = false;
    }
    if (starterAssistEnabled && (!hasStarter || !hasN1Rpm)) {
        Serial.println("[HWCfg] Starter assist disabled: requires starter output and N1 RPM feedback");
        starterAssistEnabled = false;
    }

    auto saf = doc["safety"];
    if (!saf["overspeed"].isNull()) safetyOverspeed = saf["overspeed"].as<bool>();
    if (!saf["overtemp"].isNull())  safetyOvertemp  = saf["overtemp"].as<bool>();
    if (!saf["low_oil"].isNull())   safetyLowOil    = saf["low_oil"].as<bool>();
    if (!saf["oil_zero"].isNull())  safetyOilZero   = saf["oil_zero"].as<bool>();
    if (!saf["flameout"].isNull())   safetyFlameout  = saf["flameout"].as<bool>();
    if (!saf["hot_start"].isNull())      safetyHotStart      = saf["hot_start"].as<bool>();
    if (!saf["tit_overtemp"].isNull())   safetyTitOvertemp   = saf["tit_overtemp"].as<bool>();
    if (!saf["oil_temp_high"].isNull())  safetyOilTempHigh   = saf["oil_temp_high"].as<bool>();
    if (!saf["fuel_press_low"].isNull()) safetyFuelPressLow  = saf["fuel_press_low"].as<bool>();
    if (!saf["batt_low"].isNull())       safetyBattLow       = saf["batt_low"].as<bool>();
    if (!saf["surge"].isNull())          safetySurge         = saf["surge"].as<bool>();
    if (!hasN1Rpm) {
        safetyOverspeed = false;
        safetySurge = false;
    }
    if (!hasTot && !hasTit) {
        safetyOvertemp = false;
        safetyHotStart = false;
    }
    if (!hasOilPress) {
        safetyLowOil = false;
        safetyOilZero = false;
    }
    if (!hasFlame && !hasN1Rpm && !hasTot && !hasTit) safetyFlameout = false;
    if (!hasTit) safetyTitOvertemp = false;
    if (!hasOilTemp) safetyOilTempHigh = false;
    if (!hasFuelPress) safetyFuelPressLow = false;
    if (!hasBattVoltage) safetyBattLow = false;

    if (doc["startup_seq"].is<JsonArrayConst>()) {
        JsonArrayConst ss = doc["startup_seq"];
        int n = (int)ss.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        startupSeqLen = n;
        memset(startupDelayMs, 0, sizeof(startupDelayMs));
        for (int i = 0; i < n; i++) {
            strncpy(startupSeq[i], ss[i] | "", sizeof(startupSeq[i]) - 1);
            startupSeq[i][sizeof(startupSeq[i]) - 1] = '\0';
        }
    }
    if (doc["startup_delay_ms"].is<JsonArrayConst>()) {
        JsonArrayConst d = doc["startup_delay_ms"];
        for (int i = 0; i < startupSeqLen && i < (int)d.size(); i++)
            startupDelayMs[i] = constrain(d[i] | 0, 0, 3600000);
    }
    memset(startupIgnitionTarget, 0, sizeof(startupIgnitionTarget));
    if (doc["startup_ignition_target"].is<JsonArrayConst>()) {
        JsonArrayConst t = doc["startup_ignition_target"];
        for (int i = 0; i < startupSeqLen && i < (int)t.size(); i++)
            startupIgnitionTarget[i] = constrain(t[i] | 0, 0, 2);
    }
    readSeqSideActions(doc, "startup_enter_actions", startupSeqLen, startupEnterActions);
    readSeqSideActions(doc, "startup_exit_actions", startupSeqLen, startupExitActions);

    if (doc["shutdown_seq"].is<JsonArrayConst>()) {
        JsonArrayConst ds = doc["shutdown_seq"];
        int n = (int)ds.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        shutdownSeqLen = n;
        memset(shutdownDelayMs, 0, sizeof(shutdownDelayMs));
        for (int i = 0; i < n; i++) {
            strncpy(shutdownSeq[i], ds[i] | "", sizeof(shutdownSeq[i]) - 1);
            shutdownSeq[i][sizeof(shutdownSeq[i]) - 1] = '\0';
        }
    }
    if (doc["shutdown_delay_ms"].is<JsonArrayConst>()) {
        JsonArrayConst d = doc["shutdown_delay_ms"];
        for (int i = 0; i < shutdownSeqLen && i < (int)d.size(); i++)
            shutdownDelayMs[i] = constrain(d[i] | 0, 0, 3600000);
    }
    memset(shutdownIgnitionTarget, 0, sizeof(shutdownIgnitionTarget));
    if (doc["shutdown_ignition_target"].is<JsonArrayConst>()) {
        JsonArrayConst t = doc["shutdown_ignition_target"];
        for (int i = 0; i < shutdownSeqLen && i < (int)t.size(); i++)
            shutdownIgnitionTarget[i] = constrain(t[i] | 0, 0, 2);
    }
    readSeqSideActions(doc, "shutdown_enter_actions", shutdownSeqLen, shutdownEnterActions);
    readSeqSideActions(doc, "shutdown_exit_actions", shutdownSeqLen, shutdownExitActions);

    auto abt = doc["ab_trigger"];
    abTriggerSource    = abt["source"]          | abTriggerSource;
    if (!abt["requires_arm"].isNull())  abRequiresArmSwitch = abt["requires_arm"].as<bool>();
    abArmSwitchPin     = abt["arm_pin"]         | abArmSwitchPin;
    if (!abt["arm_active_h"].isNull())  abArmSwitchActiveH  = abt["arm_active_h"].as<bool>();
    abSwitchPin        = abt["switch_pin"]      | abSwitchPin;
    if (!abt["switch_active_h"].isNull()) abSwitchActiveH   = abt["switch_active_h"].as<bool>();
    abInputPin         = abt["input_pin"]       | abInputPin;
    if (!abt["input_rc_pwm"].isNull()) abInputRcPwm = abt["input_rc_pwm"].as<bool>();
    abInputMinUs       = abt["input_min_us"]    | abInputMinUs;
    abInputMaxUs       = abt["input_max_us"]    | abInputMaxUs;
    abInputThreshold   = abt["input_threshold"] | abInputThreshold;

    auto abfl = doc["ab_flame"];
    if (!abfl["enabled"].isNull()) hasAbFlame   = hasAfterburner && abfl["enabled"].as<bool>();
    abFlamePin         = abfl["pin"]       | abFlamePin;
    abFlameThreshold   = abfl["threshold"] | abFlameThreshold;

    if (doc["ab_seq"].is<JsonArrayConst>()) {
        JsonArrayConst as = doc["ab_seq"];
        int n = (int)as.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        abSeqLen = n;
        memset(abDelayMs, 0, sizeof(abDelayMs));
        for (int i = 0; i < n; i++) {
            strncpy(abSeq[i], as[i] | "", sizeof(abSeq[i]) - 1);
            abSeq[i][sizeof(abSeq[i]) - 1] = '\0';
        }
    }
    if (doc["ab_delay_ms"].is<JsonArrayConst>()) {
        JsonArrayConst d = doc["ab_delay_ms"];
        for (int i = 0; i < abSeqLen && i < (int)d.size(); i++)
            abDelayMs[i] = constrain(d[i] | 0, 0, 3600000);
    }
    memset(abIgnitionTarget, 0, sizeof(abIgnitionTarget));
    if (doc["ab_ignition_target"].is<JsonArrayConst>()) {
        JsonArrayConst t = doc["ab_ignition_target"];
        for (int i = 0; i < abSeqLen && i < (int)t.size(); i++)
            abIgnitionTarget[i] = constrain(t[i] | 0, 0, 2);
    }
    readSeqSideActions(doc, "ab_enter_actions", abSeqLen, abEnterActions);
    readSeqSideActions(doc, "ab_exit_actions", abSeqLen, abExitActions);

    if (doc["ab_shut_seq"].is<JsonArrayConst>()) {
        JsonArrayConst ass = doc["ab_shut_seq"];
        int n = (int)ass.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        abShutSeqLen = n;
        memset(abShutDelayMs, 0, sizeof(abShutDelayMs));
        for (int i = 0; i < n; i++) {
            strncpy(abShutSeq[i], ass[i] | "", sizeof(abShutSeq[i]) - 1);
            abShutSeq[i][sizeof(abShutSeq[i]) - 1] = '\0';
        }
    }
    if (doc["ab_shut_delay_ms"].is<JsonArrayConst>()) {
        JsonArrayConst d = doc["ab_shut_delay_ms"];
        for (int i = 0; i < abShutSeqLen && i < (int)d.size(); i++)
            abShutDelayMs[i] = constrain(d[i] | 0, 0, 3600000);
    }
    memset(abShutIgnitionTarget, 0, sizeof(abShutIgnitionTarget));
    if (doc["ab_shut_ignition_target"].is<JsonArrayConst>()) {
        JsonArrayConst t = doc["ab_shut_ignition_target"];
        for (int i = 0; i < abShutSeqLen && i < (int)t.size(); i++)
            abShutIgnitionTarget[i] = constrain(t[i] | 0, 0, 2);
    }
    readSeqSideActions(doc, "ab_shut_enter_actions", abShutSeqLen, abShutEnterActions);
    readSeqSideActions(doc, "ab_shut_exit_actions", abShutSeqLen, abShutExitActions);
    readCustomBlocks(doc);
    sanitizeSeqSideActions(startupEnterActions);
    sanitizeSeqSideActions(startupExitActions);
    sanitizeSeqSideActions(shutdownEnterActions);
    sanitizeSeqSideActions(shutdownExitActions);
    sanitizeSeqSideActions(abEnterActions);
    sanitizeSeqSideActions(abExitActions);
    sanitizeSeqSideActions(abShutEnterActions);
    sanitizeSeqSideActions(abShutExitActions);

    sanitizeSequenceBlocks(startupSeq, startupSeqLen, startupDelayMs, startupIgnitionTarget, startupEnterActions, startupExitActions);
    sanitizeSequenceBlocks(shutdownSeq, shutdownSeqLen, shutdownDelayMs, shutdownIgnitionTarget, shutdownEnterActions, shutdownExitActions);
    sanitizeSequenceBlocks(abSeq, abSeqLen, abDelayMs, abIgnitionTarget, abEnterActions, abExitActions);
    sanitizeSequenceBlocks(abShutSeq, abShutSeqLen, abShutDelayMs, abShutIgnitionTarget, abShutEnterActions, abShutExitActions);

    if (doc["labels"].is<JsonObjectConst>()) {
        auto lbld = doc["labels"].as<JsonObjectConst>();
        auto cpylbl = [](char* dst, size_t sz, const char* src) {
            if (src && src[0]) { strncpy(dst, src, sz-1); dst[sz-1]='\0'; }
        };
        cpylbl(labelTot,       sizeof(labelTot),       lbld["tot"]        | "");
        cpylbl(labelTit,       sizeof(labelTit),       lbld["tit"]        | "");
        cpylbl(labelN1,        sizeof(labelN1),        lbld["n1"]         | "");
        cpylbl(labelN2,        sizeof(labelN2),        lbld["n2"]         | "");
        cpylbl(labelOilPress,  sizeof(labelOilPress),  lbld["oil_press"]  | "");
        cpylbl(labelOilTemp,   sizeof(labelOilTemp),   lbld["oil_temp"]   | "");
        cpylbl(labelP1,        sizeof(labelP1),        lbld["p1"]         | "");
        cpylbl(labelP2,        sizeof(labelP2),        lbld["p2"]         | "");
        cpylbl(labelFuelPress, sizeof(labelFuelPress), lbld["fuel_press"] | "");
        cpylbl(labelFuelFlow,  sizeof(labelFuelFlow),  lbld["fuel_flow"]  | "");
        cpylbl(labelStop,      sizeof(labelStop),      lbld["stop"]       | "");
        cpylbl(labelStart,     sizeof(labelStart),     lbld["start"]      | "");
        cpylbl(labelAbArm,     sizeof(labelAbArm),     lbld["ab_arm"]     | "");
    }

    if (doc["di_channels"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["di_channels"].as<JsonArrayConst>();
        int i = 0;
        for (JsonObjectConst ch : arr) {
            if (i >= MAX_DI) break;
            diCh[i].pin        = ch["pin"]         | -1;
            diCh[i].activeH    = ch["active_h"]    | false;
            diCh[i].debounceMs = ch["debounce_ms"] | 20;
            strncpy(diCh[i].label,     ch["label"]      | "", sizeof(diCh[i].label)-1);
            strncpy(diCh[i].role,      ch["role"]       | "none", sizeof(diCh[i].role)-1);
            strncpy(diCh[i].faultCode, ch["fault_code"] | "", sizeof(diCh[i].faultCode)-1);
            strncpy(diCh[i].faultMsg,  ch["fault_msg"]  | "", sizeof(diCh[i].faultMsg)-1);
            diCh[i].label[sizeof(diCh[i].label) - 1] = '\0';
            diCh[i].role[sizeof(diCh[i].role) - 1] = '\0';
            diCh[i].faultCode[sizeof(diCh[i].faultCode) - 1] = '\0';
            diCh[i].faultMsg[sizeof(diCh[i].faultMsg) - 1] = '\0';
            {
                // Accept out-of-range active_modes and mask to the 5 valid
                // SysMode bits instead of failing the whole config.
                int am = ch["active_modes"].isNull() ? 0x1F : ch["active_modes"].as<int>();
                if (!ch["active_modes"].isNull() && (am < 0 || am > 0x1F)) {
                    Serial.printf("[HWCfg] WARNING: DI%d active_modes %d out of range - masked to 0x1F\n",
                                  i + 1, am);
                }
                diCh[i].activeModes = (uint8_t)(am & 0x1F);
            }
            i++;
        }
    }

    if (n1RpmPpr <= 0.0f) n1RpmPpr = 1.0f;
    if (n2RpmPpr <= 0.0f) n2RpmPpr = 1.0f;
    if (igniterDwellMs < 1) igniterDwellMs = 1;
    if (igniterRestMs < 1) igniterRestMs = 1;
    if (igniter2DwellMs < 1) igniter2DwellMs = 1;
    if (igniter2RestMs < 1) igniter2RestMs = 1;
    if (mavlinkIntervalMs < 20) mavlinkIntervalMs = 100;
    if (clusterIntervalMs < 10) clusterIntervalMs = 50;
    abTriggerSource = constrain(abTriggerSource, 0, 3);
    abInputThreshold = constrain(abInputThreshold, 0, 4095);
    if (abTriggerSource == 0) abRequiresArmSwitch = false;
    if (starterEnDelayMs < 0) starterEnDelayMs = 0;
    if (fuelFlowType < 0 || fuelFlowType > 1) fuelFlowType = 0;
    auto validTcChipName = [](const char* chip) {
        return strcmp(chip, "max6675") == 0 ||
               strcmp(chip, "max31855") == 0 ||
               strcmp(chip, "max31856") == 0;
    };
    auto setChip = [](char* dst, size_t len, const char* value) {
        strncpy(dst, value, len - 1);
        dst[len - 1] = '\0';
    };
    if (!validTcChipName(totChip)) setChip(totChip, sizeof(totChip), "max6675");
    if (!validTcChipName(titChip)) setChip(titChip, sizeof(titChip), "max6675");
    if (!(strcmp(oilTempChip, "ntc") == 0 ||
          strcmp(oilTempChip, "ds18b20") == 0 ||
          validTcChipName(oilTempChip))) {
        setChip(oilTempChip, sizeof(oilTempChip), "ntc");
    }
    throttleType = constrain(throttleType, 0, 2);
    starterType = constrain(starterType, 0, 2);
    oilPumpType = constrain(oilPumpType, 0, 2);
    coolFanType = constrain(coolFanType, 0, 2);
    abPumpType = constrain(abPumpType, 0, 2);
    oilScavPumpType = constrain(oilScavPumpType, 0, 2);
    fuelPump2Type = constrain(fuelPump2Type, 0, 2);
    bleedValveType = constrain(bleedValveType, 0, 2);
    propPitchType = constrain(propPitchType, 0, 2);
    if (fuelFlowPulsesPerLitre <= 0.0f) fuelFlowPulsesPerLitre = 100.0f;
    if (oilTempResolution < 9 || oilTempResolution > 12) oilTempResolution = 12;
    if (ntcBeta < 1000.0f || ntcBeta > 10000.0f) ntcBeta = 3950.0f;
    if (ntcR0 < 100.0f || ntcR0 > 1000000.0f) ntcR0 = 10000.0f;
    if (ntcRFixed < 100.0f || ntcRFixed > 1000000.0f) ntcRFixed = 10000.0f;

    auto sanitizePwm = [](int& freqHz, int& resBits, int defaultFreq, int defaultBits) {
        if (freqHz < 1) freqHz = defaultFreq;
        if (resBits < 8 || resBits > 14) resBits = defaultBits;
    };
    sanitizePwm(throttleLedcFreqHz, throttleLedcBits, 10000, 12);
    sanitizePwm(starterLedcFreqHz, starterLedcBits, 10000, 12);
    sanitizePwm(oilPumpFreqHz, oilPumpResBits, 10000, 12);
    sanitizePwm(coolFanFreqHz, coolFanResBits, 10000, 12);
    sanitizePwm(abPumpFreqHz, abPumpResBits, 10000, 12);
    sanitizePwm(oilScavPumpFreqHz, oilScavPumpResBits, 10000, 12);
    sanitizePwm(fuelPump2FreqHz, fuelPump2ResBits, 10000, 12);
    sanitizePwm(bleedValveFreqHz, bleedValveResBits, 1000, 10);
    sanitizePwm(propPitchFreqHz, propPitchResBits, 1000, 10);
    sanitizePwm(glowPlugFreqHz, glowPlugResBits, 1000, 8);
    sanitizePwm(wetGlowFuelFreqHz, wetGlowFuelResBits, 1000, 10);

    auto sanitizePwmRange = [](float& minPct, float& maxPct) {
        minPct = constrain(minPct, 0.0f, 100.0f);
        maxPct = constrain(maxPct, 0.0f, 100.0f);
        if (maxPct < minPct) {
            minPct = 0.0f;
            maxPct = 100.0f;
        }
    };
    sanitizePwmRange(throttlePwmMinPct, throttlePwmMaxPct);
    sanitizePwmRange(starterPwmMinPct, starterPwmMaxPct);
    sanitizePwmRange(oilPumpPwmMinPct, oilPumpPwmMaxPct);
    sanitizePwmRange(coolFanPwmMinPct, coolFanPwmMaxPct);
    sanitizePwmRange(abPumpPwmMinPct, abPumpPwmMaxPct);
    sanitizePwmRange(oilScavPumpPwmMinPct, oilScavPumpPwmMaxPct);
    sanitizePwmRange(fuelPump2PwmMinPct, fuelPump2PwmMaxPct);
    sanitizePwmRange(bleedValvePwmMinPct, bleedValvePwmMaxPct);
    sanitizePwmRange(propPitchPwmMinPct, propPitchPwmMaxPct);
    sanitizePwmRange(glowPlugPwmMinPct, glowPlugPwmMaxPct);
    sanitizePwmRange(wetGlowFuelPwmMinPct, wetGlowFuelPwmMaxPct);
    for (int i = 0; i < MAX_DI; i++) {
        if (!(strcmp(diCh[i].role, "none") == 0 ||
              strcmp(diCh[i].role, "fault") == 0 ||
              strcmp(diCh[i].role, "estop") == 0 ||
              strcmp(diCh[i].role, "inhibit_start") == 0 ||
              strcmp(diCh[i].role, "sequence_gate") == 0 ||
              strcmp(diCh[i].role, "ab_arm") == 0 ||
              strcmp(diCh[i].role, "ab_fire") == 0 ||
              strcmp(diCh[i].role, "limp_mode") == 0)) {
            strncpy(diCh[i].role, "none", sizeof(diCh[i].role) - 1);
            diCh[i].role[sizeof(diCh[i].role) - 1] = '\0';
        }
        if (diCh[i].debounceMs < 5 || diCh[i].debounceMs > 500) diCh[i].debounceMs = 20;
        diCh[i].activeModes &= 0x1F;
    }

    auto sanitizeServoRange = [](int& minUs, int& maxUs) {
        minUs = constrain(minUs, 500, 2500);
        maxUs = constrain(maxUs, 500, 2500);
        if (maxUs <= minUs) {
            minUs = 1000;
            maxUs = 2000;
        }
    };
    sanitizeServoRange(throttleMinUs, throttleMaxUs);
    sanitizeServoRange(starterMinUs, starterMaxUs);
    sanitizeServoRange(oilPumpMinUs, oilPumpMaxUs);
    sanitizeServoRange(coolFanMinUs, coolFanMaxUs);
    sanitizeServoRange(abPumpMinUs, abPumpMaxUs);
    sanitizeServoRange(abInputMinUs, abInputMaxUs);
    sanitizeServoRange(oilScavPumpMinUs, oilScavPumpMaxUs);
    sanitizeServoRange(fuelPump2MinUs, fuelPump2MaxUs);
    sanitizeServoRange(bleedValveMinUs, bleedValveMaxUs);
    sanitizeServoRange(propPitchMinUs, propPitchMaxUs);

    // Deterministic compatibility migration.  The old singleton fields remain
    // live adapters for this release, but all generated IDs are stable so
    // callers can begin persisting references without relying on labels.
    if (channelRegistry.inputCount == 0 && channelRegistry.outputCount == 0) {
        auto addInput = [](const char* id, const char* role, int pin, ChannelRegistry::Driver driver) {
            ChannelRegistry::Channel c; c.installed = true; c.direction = ChannelRegistry::Input;
            c.driver = driver; c.pin = pin; strlcpy(c.id, id, sizeof(c.id)); strlcpy(c.name, id, sizeof(c.name)); strlcpy(c.role, role, sizeof(c.role));
            HardwareConfig::channelRegistry.add(c);
        };
        auto addOutput = [](const char* id, const char* role, int pin, int legacyType) {
            ChannelRegistry::Channel c; c.installed = true; c.direction = ChannelRegistry::Output;
            c.driver = legacyType == 0 ? ChannelRegistry::Servo : legacyType == 1 ? ChannelRegistry::Pwm : ChannelRegistry::Relay;
            c.pin = pin; strlcpy(c.id, id, sizeof(c.id)); strlcpy(c.name, id, sizeof(c.name)); strlcpy(c.role, role, sizeof(c.role));
            HardwareConfig::channelRegistry.add(c);
        };
        if (hasN1Rpm) addInput("n1_main", "speed", n1RpmPin, ChannelRegistry::Pulse);
        if (hasN2Rpm) addInput("n2_main", "speed", n2RpmPin, ChannelRegistry::Pulse);
        if (hasOilPress) addInput("oil_pressure_main", "pressure", oilPressPin, ChannelRegistry::Analog);
        if (hasThrottle) addOutput("main_fuel", "fuel", throttlePin, throttleType);
        if (hasStarter) addOutput("starter_main", "starter", starterPin, starterType);
        if (hasOilPump) addOutput("oil_pump_main", "oil_pump", oilPumpPin, oilPumpType);
        if (hasCoolFan) addOutput("cooling_fan_main", "cooling_fan", coolFanPin, coolFanType);
        if (hasBleedValve) addOutput("bleed_valve_main", "valve", bleedValvePin, bleedValveType);
        if (hasOilScavengePump) addOutput("oil_scavenge_main", "scavenge_pump", oilScavPumpPin, oilScavPumpType);
    }
    if (oilLoopCount == 0 && hasOilLoop && hasOilPress && hasOilPump) {
        const ChannelRegistry::Channel* pressure = channelRegistry.find("oil_pressure_main", ChannelRegistry::Input);
        const ChannelRegistry::Channel* pump = channelRegistry.find("oil_pump_main", ChannelRegistry::Output);
        if (!pressure) for (uint8_t i = 0; i < channelRegistry.inputCount; i++)
            if (!strcmp(channelRegistry.inputs[i].role, "pressure")) { pressure = &channelRegistry.inputs[i]; break; }
        if (!pump) for (uint8_t i = 0; i < channelRegistry.outputCount; i++)
            if (!strcmp(channelRegistry.outputs[i].role, "oil_pump")) { pump = &channelRegistry.outputs[i]; break; }
        if (pressure && pump) {
            OilLoopDef& l = oilLoops[oilLoopCount++];
            l.enabled = true;
            strlcpy(l.id, "main_oil_loop", sizeof(l.id));
            l.pressureInputIndex = (uint8_t)(pressure - channelRegistry.inputs);
            l.pumpOutputIndex = (uint8_t)(pump - channelRegistry.outputs);
            l.targetCentiBar = (uint16_t)constrain((int)(Config::oilStartupPressure * 100.0f), 0, 2000);
            l.deadbandCentiBar = (uint16_t)constrain((int)(Config::oilPressureDeadband * 100.0f), 0, 500);
            l.minDemandPct = (uint8_t)constrain((int)Config::oilMinPct, 0, 100);
            l.maxDemandPct = 100;
        }
    }
}
