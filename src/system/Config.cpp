#include "Config.h"
#include "HardwareConfig.h"
#include "hardware_profile.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <Arduino.h>
#include <math.h>

// ── Static member definitions ─────────────────────────────────
float Config::rpmLimit              = 100000;
float Config::minRpm                = 30000;
float Config::totLimit              = 750;
float Config::totCooldownTarget     = 150;
float Config::totSafeMargin         = 50;

float Config::oilStartupPressure    = 2.5f;
float Config::oilStartupPct         = 80.0f; // pump % when no oil pressure sensor
float Config::oilStartupMinBar      = 1.5f;
float Config::oilRunningMin         = 2.8f;
float Config::oilMapMin             = 3.6f;
float Config::oilMapMax             = 4.4f;
bool  Config::oilUseThrottleMap     = false;
float Config::oilAdjustScale        = 1.80f;
float Config::oilMinPct             = 18.0f;
int   Config::oilFailsafeDelayMs    = 1500;
float Config::oilFailsafePct        = 60.0f;

int   Config::startupOilArmTimeoutMs  = 3000;
float Config::preIgnRpm               = 5000;
int   Config::preIgnSparkMs           = 1500;
int   Config::flameTimeoutMs          = 5000;
int   Config::flameCheckIntervalMs    = 300;
float Config::spoolRpmTarget          = 32000;
int   Config::spoolTimeoutMs          = 12000;
int   Config::safetyHoldMs            = 1000;
float Config::safetyHoldFinalRpm      = 31000;
float Config::shutdownRpmDropThreshold= 5000;
int   Config::shutdownRpmDropTimeoutMs= 15000;
int   Config::shutdownCooldownTimeoutMs= 60000;  // 60 s default (was 200 s — unreachably long for typical engines)
int   Config::shutdownFinalStopTimeoutMs=10000;

float Config::throttleRampUpMs      = 600;
float Config::throttleRampDownMs    = 800;
float Config::throttleIdleMinPct    = 8;
float Config::throttleIdleMaxPct    = 18;
float Config::throttleExpo          = 0.0f;  // 0 = linear by default

float Config::idleTargetRpm         = 44000;
float Config::idleRampUpMs          = 10000;
float Config::idleRampDownMs        = 20000;
float Config::idleDeadbandRpm       = 300;
float Config::idleRpmLimit          = 60000;
float Config::idleMinMultiplier     = 0.75f;
bool  Config::idleUseN2             = false;
float Config::idleIGain             = 0.0f;   // 0 = off by default (pure ramp mode), enable in config
float Config::idleIMax              = 0.10f;  // ±10% integral authority

int   Config::safetyCheckIntervalMs      = 100;
float Config::flameoutShutdownMs         = 3000;
int   Config::egtSource                  = 0;
int   Config::flameoutSource             = 0;
float Config::flameoutN1MinRpm           = 0.0f;
float Config::flameoutTotDropC           = 80.0f;
float Config::totRiseRateLimitDegPerSec  = 0.0f;
float Config::titLimit                   = 0.0f;
float Config::oilTempLimit               = 120.0f;
float Config::fuelPressMin               = 0.0f;
float Config::battVoltMin                = 0.0f;
float Config::surgeDetectRpmVariance     = 0.0f;

bool     Config::relightEnabled      = false;
int      Config::relightConfirmSource = 0;
float    Config::relightMinRpm       = 30000.0f;
float    Config::relightConfirmRpm   = 0.0f;
float    Config::relightTotRiseC     = 30.0f;
int      Config::relightTimeoutMs    = 10000;   // 10 s continuous ignition window before faulting

uint32_t Config::toolFuelPrimeMs    = 3000;
uint32_t Config::toolOilPrimeMs     = 5000;
uint32_t Config::toolIgnTestMs      = 2000;
uint32_t Config::toolStartTestMs    = 2000;
uint32_t Config::toolFuelSolTestMs  = 1000;

uint32_t Config::wsIntervalMs       = 333;
uint32_t Config::snapshotIntervalMs = 10000;
bool     Config::logStandby         = false;

float    Config::starterAssistPct    = 15.0f;
float    Config::starterAssistExitRpm = 1000.0f;

float    Config::starterRampPctPerSec = 10.0f;
float    Config::starterDemand        = 60.0f;  // %
int      Config::starterTimeoutMs     = 8000;

float    Config::tempConfirmTarget    = 200.0f;
int      Config::tempConfirmTimeoutMs = 10000;

float    Config::rpmZeroThreshold     = 100.0f;

float    Config::oilZeroBar          = 0.1f;
float    Config::oilPressureDeadband = 0.2f;

int      Config::standbyOilSource    = 0;
float    Config::standbyOilRpmLimit  = 100.0f;
float    Config::standbyOilFeedPct   = 25.0f;

float    Config::limpMaxThrottlePct  = 50.0f;
bool     Config::igniterOnStart      = true;

bool     Config::cooldownUseStarter         = true;
bool     Config::cooldownUseOilPump         = true;
float    Config::cooldownStarterPct         = 40.0f;  // %
float    Config::cooldownOilPct             = 30.0f;  // % (no pressure sensor)
float    Config::cooldownOilPressureTarget  = 2.0f;   // bar

int      Config::flameRequiredCount  = 3;

int      Config::waitForInputChannel   = 0;
bool     Config::waitForInputExpected  = true;
int      Config::waitForInputTimeoutMs = 0;

int      Config::cooldownSkipHoldMs  = 1000;

float    Config::fuelPumpIdleMinPct  = 8.0f;
float    Config::fuelPumpIdleMaxPct  = 18.0f;

int      Config::timedDelayMs            = 1000;
float    Config::modifiedIdleMultiplier  = 1.0f;
int      Config::fuelPulsePulseMs        = 200;
int      Config::fuelPulseOffMs          = 300;
float    Config::waitTotCoolTarget       = 150.0f;
int      Config::waitTotCoolTimeoutMs    = 120000;
float    Config::throttleSetPct          = 10.0f;
int      Config::preHeatMs               = 3000;
float    Config::oilPumpOnPct            = 100.0f;

bool     Config::flameConfirmTurnOffIgniter  = true;
bool     Config::safetyHoldTurnOffStarter    = false;
bool     Config::safetyHoldTurnOffStarterEn  = false;
bool     Config::safetyHoldTurnOffIgniter    = false;
bool     Config::spoolCutStarterOnExit       = true;
bool     Config::spoolCutStarterEnOnExit     = true;

float    Config::hotStartTotThreshold        = 0.0f;
int      Config::finalStopOilScavengeMs      = 0;
bool     Config::oilPrimeUseScavengePump    = false;
bool     Config::cooldownUseScavengePump    = false;

float    Config::abMinN1                    = 30000.0f;
float    Config::abMaxN1                    = 0.0f;      // 0 = disabled
float    Config::abMaxTotForLight           = 0.0f;      // 0 = disabled
float    Config::abThrottleThreshold        = 0.80f;     // 80%
bool     Config::abUseTorch                 = true;
bool     Config::abUseIgniter               = false;
float    Config::abTorchSpikePct            = 30.0f;
int      Config::abTorchDurationMs          = 400;
float    Config::abTorchTotLimit            = 0.0f;      // 0 = disabled
int      Config::abFlameMode                = 2;         // 2=timed (safest default)
float    Config::abTotRiseDegC              = 30.0f;
int      Config::abTotRiseWindowMs          = 2000;
int      Config::abAssumeIgnitedMs          = 1500;
int      Config::abFlameTimeoutMs           = 3000;
float    Config::abPumpMinPct               = 80.0f;
float    Config::abPumpMaxPct               = 100.0f;
int      Config::abPumpControlMode          = 0;
bool     Config::abPumpFollowThrottle       = false;
float    Config::abMainFuelOffsetPct        = 0.0f;
int      Config::abStabilizeMs              = 1000;
float    Config::abStabilizeMaxTot          = 0.0f;      // 0 = disabled

float    Config::rpmJumpThreshold    = 0.40f;
int      Config::rpmZeroStuckTicks   = 5;

float    Config::n1WarnRpm          = 90000.0f;   // default = rpmLimit * 0.9
float    Config::n2WarnRpm          = 22000.0f;
float    Config::totWarnC           = 0.0f;       // 0 = auto (selected EGT limit - totSafeMargin)
float    Config::oilWarnBar         = 0.0f;       // 0 = auto (oilRunningMin)
bool     Config::clusterEnabled     = true;

bool     Config::pressureSensorsEnabled = false;

int      Config::rcMinUs            = 1000;
int      Config::rcMaxUs            = 2000;
int      Config::rcFailsafeMs       = 500;

uint32_t Config::sessionLogMask       = Config::SLOG_DEFAULT;
uint32_t Config::sessionLogIntervalMs = 1000;  // 1 Hz default
float Config::governorTargetRpm     = 0.0f;
float Config::governorBandRpm       = 500.0f;
float Config::governorKp            = 0.001f;
float Config::governorPitchKp       = 0.0005f;
float Config::governorPitchRampSec  = 10.0f;   // 0→100% pitch in 10 s max
int   Config::govHoldTimeoutMs      = 10000;
float Config::fp2StartPct           = 0.0f;
float Config::fp2EndPct             = 80.0f;
int   Config::fp2RampMs             = 3000;
float Config::fp2DemandPct          = 0.0f;
float Config::propPitchIdleDeg      = 15.0f;
float Config::propPitchMaxDeg       = 35.0f;

int   Config::glowPreheatMs         = 10000;
float Config::glowPreheatMaxPct     = 80.0f;
float Config::glowHoldPct           = 30.0f;
bool  Config::glowWaitUntilHot      = false;

uint32_t Config::totalRunSeconds    = 0;

int   Config::throttleMinRaw        = 0;
int   Config::throttleMaxRaw        = 4095;
int   Config::idleMinRaw            = 0;
int   Config::idleMaxRaw            = 4095;
int   Config::flameThreshold        = 500;
float Config::oilPolyA              = 0;
float Config::oilPolyB              = 0;
float Config::oilPolyC              = 0;
float Config::oilPolyD              = 0;
float Config::oilPolyXMin           = 0;
float Config::oilPolyXMax           = 4095;
int   Config::p1RawMin              = 0;
int   Config::p1RawMax              = 4095;
float Config::p1ValMax              = 10.0f;
int   Config::p2RawMin              = 0;
int   Config::p2RawMax              = 4095;
float Config::p2ValMax              = 10.0f;
float Config::p1ZeroBar             = 0.0f;
float Config::p2ZeroBar             = 0.0f;
int   Config::fuelPressRawMin       = 0;
int   Config::fuelPressRawMax       = 4095;
float Config::fuelPressValMax       = 10.0f;
int   Config::fuelFlowRawMin        = 0;
int   Config::fuelFlowRawMax        = 4095;
float Config::fuelFlowValMax        = 10.0f;

char  Config::profileId[64]         = {};
bool  Config::profileMatch          = false;
static SemaphoreHandle_t s_configWriteMutex = nullptr;

static void inhibitStartForConfigWriteFailure() {
    strncpy(EngineData::instance().faultDescription,
        "Cannot start: the ECU configuration could not be written to storage. "
        "Check or re-upload the filesystem before operating the engine.",
        sizeof(EngineData::instance().faultDescription) - 1);
    EngineData::instance().faultDescription[
        sizeof(EngineData::instance().faultDescription) - 1] = '\0';
    Config::profileMatch = false;
}

static void inhibitStartForProfileMismatch() {
    strncpy(EngineData::instance().faultDescription,
        "Cannot start: hardware and settings in ecu_config.json identify different engines. "
        "Restore one complete engine file or save Hardware to synchronize its profile ID.",
        sizeof(EngineData::instance().faultDescription) - 1);
    EngineData::instance().faultDescription[
        sizeof(EngineData::instance().faultDescription) - 1] = '\0';
    Config::profileMatch = false;
}

Config::Rule Config::rules[Config::MAX_RULES] = {};
int          Config::ruleCount                = 0;

namespace {
bool ruleSensorAvailable(uint8_t s) {
    switch (s) {
        case 0:  return HardwareConfig::hasOilTemp;
        case 1:  return HardwareConfig::hasTot;
        case 2:  return HardwareConfig::hasN1Rpm;
        case 3:  return HardwareConfig::hasOilPress;
        case 4:  return HardwareConfig::hasTit;
        case 5:  return HardwareConfig::hasBattVoltage;
        case 6:  return HardwareConfig::hasN2Rpm;
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
        case 19: return HardwareConfig::hasAbFlame;
        case 20: return HardwareConfig::hasGlowCurrentSensor;
        case 21: return HardwareConfig::hasIgniterCurrentSensor;
        case 22: return HardwareConfig::hasIgniter2CurrentSensor;
        case 23: return HardwareConfig::hasOilPumpCurrentSensor;
        case 24: return HardwareConfig::hasAfterburner && HardwareConfig::abInputPin >= 0;
        case 25: return HardwareConfig::startPin >= 0;
        case 26: return HardwareConfig::stopPin >= 0;
        default: return false;
    }
}

bool ruleActuatorAvailable(uint8_t a) {
    switch (a) {
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
        case 13:
        case 14: return true;
        case 15: return HardwareConfig::hasAirstarterSol;
        case 16: return HardwareConfig::hasGlowPlug;
        case 17: return HardwareConfig::hasPropPitch;
        default: return false;
    }
}
} // namespace

namespace {

bool present(JsonVariantConst v) { return !v.isNull(); }

bool validNumber(JsonVariantConst v, float minValue, float maxValue) {
    if (!present(v)) return true;
    if (!v.is<float>() && !v.is<double>() && !v.is<int>() && !v.is<long>() && !v.is<unsigned long>()) return false;
    float value = v.as<float>();
    return isfinite(value) && value >= minValue && value <= maxValue;
}

bool validInt(JsonVariantConst v, long minValue, long maxValue) {
    if (!present(v)) return true;
    if (!v.is<int>() && !v.is<long>() && !v.is<unsigned int>() && !v.is<unsigned long>()) return false;
    long value = v.as<long>();
    return value >= minValue && value <= maxValue;
}

bool validBool(JsonVariantConst v) { return !present(v) || v.is<bool>(); }

bool validRawPair(JsonVariantConst obj, const char* minKey, const char* maxKey) {
    if (!validInt(obj[minKey], 0, 4095) || !validInt(obj[maxKey], 0, 4095)) return false;
    if (present(obj[minKey]) && present(obj[maxKey]) && obj[maxKey].as<int>() <= obj[minKey].as<int>()) return false;
    return true;
}

bool validMsFields(JsonVariantConst obj, const char* const* keys, int count, long maxValue = 3600000) {
    for (int i = 0; i < count; i++)
        if (!validInt(obj[keys[i]], 0, maxValue)) return false;
    return true;
}

bool validateSettingsDoc(const JsonDocument& doc) {
    const char* id = doc["profile_id"] | "";
    if (!id[0] || strlen(id) >= sizeof(Config::profileId)) return false;

    const char* requiredSections[] = { "engine", "oil", "sequence", "throttle", "safety", "calibration" };
    for (const char* section : requiredSections)
        if (!doc[section].is<JsonObjectConst>()) return false;

    JsonVariantConst eng = doc["engine"];
    if (!validNumber(eng["rpm_limit"], 1000.0f, 500000.0f) ||
        !validNumber(eng["min_rpm"], 0.0f, 500000.0f) ||
        !validNumber(eng["tot_limit"], 1.0f, 1400.0f) ||
        !validNumber(eng["tot_cooldown_target"], 0.0f, 1400.0f) ||
        !validNumber(eng["tot_safe_margin"], 0.0f, 1400.0f)) return false;
    if (present(eng["rpm_limit"]) && present(eng["min_rpm"]) && eng["min_rpm"].as<float>() >= eng["rpm_limit"].as<float>()) return false;
    if (present(eng["tot_limit"]) && present(eng["tot_cooldown_target"]) && eng["tot_cooldown_target"].as<float>() >= eng["tot_limit"].as<float>()) return false;

    JsonVariantConst oil = doc["oil"];
    if (!validNumber(oil["startup_pressure"], 0.0f, 20.0f) ||
        !validNumber(oil["startup_pct"], 0.0f, 100.0f) ||
        !validNumber(oil["startup_min_bar"], 0.0f, 20.0f) ||
        !validNumber(oil["running_min"], 0.0f, 20.0f) ||
        !validNumber(oil["map_min"], 0.0f, 20.0f) ||
        !validNumber(oil["map_max"], 0.0f, 20.0f) ||
        !validBool(oil["use_throttle_map"]) ||
        !validNumber(oil["adjust_scale"], 0.0f, 20.0f) ||
        !validNumber(oil["min_pct"], 0.0f, 100.0f) ||
        !validInt(oil["failsafe_delay_ms"], 0, 60000) ||
        !validNumber(oil["failsafe_pct"], 0.0f, 100.0f)) return false;
    if (present(oil["map_min"]) && present(oil["map_max"]) && oil["map_max"].as<float>() < oil["map_min"].as<float>()) return false;

    JsonVariantConst su = doc["sequence"]["startup"];
    JsonVariantConst sd = doc["sequence"]["shutdown"];
    if (!su.is<JsonObjectConst>() || !sd.is<JsonObjectConst>()) return false;
    const char* startupMs[] = {
        "oil_arm_timeout_ms", "pre_ign_spark_ms", "flame_timeout_ms", "rpm_timeout_ms",
        "safety_hold_ms", "starter_timeout_ms", "temp_confirm_timeout", "wait_for_input_timeout",
        "timed_delay_ms", "fuel_pulse_ms", "fuel_off_ms", "wait_tot_timeout", "preheat_ms",
        "fp2_ramp_ms", "gov_hold_timeout_ms"
    };
    if (!validMsFields(su, startupMs, sizeof(startupMs) / sizeof(startupMs[0])) ||
        !validInt(su["flame_check_interval_ms"], 1, 3600000) ||
        !validInt(su["flame_required_count"], 1, 1000) ||
        !validInt(su["wait_for_input_ch"], 0, 3) ||
        !validBool(su["wait_for_input_state"])) return false;
    if (!validNumber(su["pre_ign_rpm"], 0.0f, 500000.0f) ||
        !validNumber(su["rpm_target"], 0.0f, 500000.0f) ||
        !validNumber(su["final_check_rpm"], 0.0f, 500000.0f) ||
        !validNumber(su["starter_demand"], 0.0f, 100.0f) ||
        !validNumber(su["temp_confirm_target"], 0.0f, 1400.0f) ||
        !validNumber(su["wait_tot_target"], 0.0f, 1400.0f) ||
        !validNumber(su["throttle_set_pct"], 0.0f, 100.0f) ||
        !validNumber(su["oil_pump_on_pct"], 0.0f, 100.0f) ||
        !validNumber(su["modified_idle_multiplier"], 0.0f, 10.0f) ||
        !validNumber(su["hot_start_tot_threshold"], 0.0f, 1400.0f) ||
        !validNumber(su["fp2_start_pct"], 0.0f, 100.0f) ||
        !validNumber(su["fp2_end_pct"], 0.0f, 100.0f) ||
        !validNumber(su["fp2_demand_pct"], 0.0f, 100.0f)) return false;
    if (!validBool(su["flame_turn_off_igniter"]) ||
        !validBool(su["safety_turn_off_starter"]) ||
        !validBool(su["safety_turn_off_starter_en"]) ||
        !validBool(su["safety_turn_off_igniter"]) ||
        !validBool(su["spool_cut_starter_on_exit"]) ||
        !validBool(su["spool_cut_starter_en_on_exit"]) ||
        !validBool(su["oil_prime_use_scavenge"])) return false;

    const char* shutdownMs[] = { "rpm_drop_timeout_ms", "cooldown_timeout_ms", "final_stop_timeout_ms", "oil_scavenge_ms" };
    if (!validMsFields(sd, shutdownMs, sizeof(shutdownMs) / sizeof(shutdownMs[0])) ||
        !validNumber(sd["rpm_drop_threshold"], 0.0f, 500000.0f) ||
        !validNumber(sd["rpm_zero_threshold"], 0.0f, 500000.0f) ||
        !validNumber(sd["cooldown_starter_pct"], 0.0f, 100.0f) ||
        !validNumber(sd["cooldown_oil_pct"], 0.0f, 100.0f) ||
        !validNumber(sd["cooldown_oil_pressure_bar"], 0.0f, 20.0f) ||
        !validBool(sd["cooldown_use_scavenge"]) ||
        !validBool(sd["cooldown_use_starter"]) ||
        !validBool(sd["cooldown_use_oil"])) return false;

    JsonVariantConst th = doc["throttle"];
    if (!validNumber(th["ramp_up_ms"], 0.0f, 3600000.0f) ||
        !validNumber(th["ramp_down_ms"], 0.0f, 3600000.0f) ||
        !validNumber(th["idle_min_pct"], 0.0f, 100.0f) ||
        !validNumber(th["idle_max_pct"], 0.0f, 100.0f) ||
        !validNumber(th["expo"], 0.0f, 1.0f)) return false;
    if (present(th["idle_min_pct"]) && present(th["idle_max_pct"]) && th["idle_max_pct"].as<float>() < th["idle_min_pct"].as<float>()) return false;

    JsonVariantConst so = doc["standby_oil"];
    if (present(so) && (!so.is<JsonObjectConst>() ||
        !validInt(so["source"], 0, 2) ||
        !validNumber(so["rpm_limit"], 0.0f, 500000.0f) ||
        !validNumber(so["feed_pct"], 0.0f, 100.0f))) return false;

    JsonVariantConst sf = doc["safety"];
    if (!validInt(sf["check_interval_ms"], 10, 60000) ||
        !validNumber(sf["flameout_shutdown_ms"], 100.0f, 60000.0f) ||
        !validInt(sf["egt_source"], 0, 2) ||
        !validInt(sf["flameout_source"], 0, 3) ||
        !validNumber(sf["flameout_n1_min_rpm"], 0.0f, 500000.0f) ||
        !validNumber(sf["flameout_tot_drop_c"], 0.0f, 1400.0f) ||
        !validNumber(sf["tot_rise_rate_limit_deg_s"], 0.0f, 1000.0f) ||
        !validNumber(sf["tit_limit_c"], 0.0f, 1400.0f) ||
        !validNumber(sf["oil_temp_limit_c"], 0.0f, 300.0f) ||
        !validNumber(sf["fuel_press_min_bar"], 0.0f, 100.0f) ||
        !validNumber(sf["batt_volt_min_v"], 0.0f, 80.0f) ||
        !validNumber(sf["surge_detect_rpm_variance"], 0.0f, 500000.0f)) return false;

    JsonVariantConst cal = doc["calibration"];
    if (!validInt(cal["throttle_min_raw"], 0, 4095) ||
        !validInt(cal["throttle_max_raw"], 0, 4095) ||
        !validInt(cal["idle_min_raw"], 0, 4095) ||
        !validInt(cal["idle_max_raw"], 0, 4095) ||
        !validInt(cal["flame_threshold"], 0, 4095) ||
        !validRawPair(cal, "p1_raw_min", "p1_raw_max") ||
        !validRawPair(cal, "p2_raw_min", "p2_raw_max") ||
        !validRawPair(cal, "fuel_press_raw_min", "fuel_press_raw_max") ||
        !validRawPair(cal, "fuel_flow_raw_min", "fuel_flow_raw_max") ||
        !validNumber(cal["p1_val_max"], 0.001f, 1000.0f) ||
        !validNumber(cal["p2_val_max"], 0.001f, 1000.0f) ||
        !validNumber(cal["fuel_press_val_max"], 0.001f, 1000.0f) ||
        !validNumber(cal["fuel_flow_val_max"], 0.001f, 1000.0f)) return false;
    if (present(cal["throttle_min_raw"]) && present(cal["throttle_max_raw"]) && cal["throttle_min_raw"].as<int>() == cal["throttle_max_raw"].as<int>()) return false;
    if (present(cal["idle_min_raw"]) && present(cal["idle_max_raw"]) && cal["idle_min_raw"].as<int>() == cal["idle_max_raw"].as<int>()) return false;

    JsonVariantConst di = doc["dynamic_idle"];
    if (present(di) && (!di.is<JsonObjectConst>() ||
        !validNumber(di["target_rpm"], 0.0f, 500000.0f) ||
        !validNumber(di["ramp_up_ms"], 0.0f, 3600000.0f) ||
        !validNumber(di["ramp_down_ms"], 0.0f, 3600000.0f) ||
        !validNumber(di["deadband_rpm"], 0.0f, 500000.0f) ||
        !validNumber(di["rpm_limit"], 0.0f, 500000.0f) ||
        !validNumber(di["min_multiplier"], 0.0f, 1.0f) ||
        !validBool(di["use_n2"]) ||
        !validNumber(di["i_gain"], 0.0f, 2.0f) ||
        !validNumber(di["i_max"], 0.0f, 0.5f))) return false;

    JsonVariantConst ab = doc["afterburner"];
    if (present(ab)) {
        if (!ab.is<JsonObjectConst>() ||
            !validNumber(ab["min_n1"], 0.0f, 500000.0f) ||
            !validNumber(ab["max_n1"], 0.0f, 500000.0f) ||
            !validNumber(ab["max_tot_for_light"], 0.0f, 1400.0f) ||
            !validNumber(ab["throttle_threshold"], 0.0f, 1.0f) ||
            !validBool(ab["use_torch"]) ||
            !validBool(ab["use_igniter"]) ||
            !validNumber(ab["torch_spike_pct"], 0.0f, 100.0f) ||
            !validInt(ab["torch_duration_ms"], 0, 3600000) ||
            !validNumber(ab["torch_tot_limit"], 0.0f, 1400.0f) ||
            !validInt(ab["flame_mode"], 0, 2) ||
            !validNumber(ab["tot_rise_deg_c"], 0.0f, 1400.0f) ||
            !validInt(ab["tot_rise_window_ms"], 0, 3600000) ||
            !validInt(ab["assume_ignited_ms"], 0, 3600000) ||
            !validInt(ab["flame_timeout_ms"], 0, 3600000) ||
            !validNumber(ab["pump_min_pct"], 0.0f, 100.0f) ||
            !validNumber(ab["pump_max_pct"], 0.0f, 100.0f) ||
            !validInt(ab["pump_control_mode"], 0, 2) ||
            !validBool(ab["pump_follow_throttle"]) ||
            !validNumber(ab["main_fuel_offset_pct"], -20.0f, 50.0f) ||
            !validInt(ab["stabilize_ms"], 0, 3600000) ||
            !validNumber(ab["stabilize_max_tot"], 0.0f, 1400.0f)) return false;
        if (present(ab["pump_min_pct"]) && present(ab["pump_max_pct"]) && ab["pump_max_pct"].as<float>() < ab["pump_min_pct"].as<float>()) return false;
    }

    JsonVariantConst sl = doc["session_log"];
    if (present(sl) && (!sl.is<JsonObjectConst>() || !validInt(sl["interval_ms"], 100, 60000))) return false;

    JsonVariantConst rules = doc["rules"];
    if (present(rules)) {
        if (!rules.is<JsonArrayConst>() || rules.size() > Config::MAX_RULES) return false;
        for (JsonObjectConst rule : rules.as<JsonArrayConst>()) {
            if (!validBool(rule["enabled"]) ||
                !validInt(rule["sensor"], 0, 24) ||
                !validInt(rule["op"], 0, 4) ||
                !validNumber(rule["threshold"], -1000000.0f, 1000000.0f) ||
                !validInt(rule["actuator"], 0, 20) ||
                !validNumber(rule["on_value"], -100.0f, 100.0f) ||
                !validNumber(rule["off_value"], -100.0f, 100.0f)) return false;
        }
    }

    return true;
}

}

// ── Load ──────────────────────────────────────────────────────
void Config::load() {
    _applyDefaults();
    EngineData::instance().configVersionMismatch = false;

    static constexpr const char* BAK_PATH = "/ecu_config.bak";
    if (!LittleFS.exists(PATH) && LittleFS.exists(BAK_PATH)) {
        if (LittleFS.rename(BAK_PATH, PATH)) {
            Serial.println("[Config] Recovered ecu_config.json from backup");
        }
    }

    // ── Migration: always check legacy file first, even if unified file exists.
    // This handles the race where HardwareConfig::load() already created
    // ecu_config.json (hardware section only) before Config has a chance to migrate.
    if (LittleFS.exists(LEGACY_PATH)) {
        File old = LittleFS.open(LEGACY_PATH, "r");
        if (old) {
            JsonDocument doc;
            if (deserializeJson(doc, old) == DeserializationError::Ok) {
                old.close();
                const char* id = doc["profile_id"] | "";
                strncpy(profileId, id, sizeof(profileId) - 1);
                profileMatch = (strcmp(profileId, HardwareConfig::profileId) == 0);
                if (!profileMatch) {
                    // Keep web repair available, but never authorize engine operation
                    // using defaults silently substituted for another profile.
                    Serial.printf("[Config] WARNING: legacy settings profile (%s) does not match hardware profile (%s)"
                                  " - START inhibited until repaired\n", profileId, HardwareConfig::profileId);
                    _applyDefaults();
                    strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
                    inhibitStartForProfileMismatch();
                    return;
                }
                _fromDoc(doc);
                if (!save()) {
                    inhibitStartForConfigWriteFailure();
                    return;
                }
                LittleFS.remove(LEGACY_PATH);
                Serial.println("[Config] Migrated config.json -> ecu_config.json");
                return;
            }
            old.close();
        }
    }

    if (!LittleFS.exists(PATH)) {
        Serial.println("[Config] No ecu_config.json — generating defaults");
        if (!save()) {
            inhibitStartForConfigWriteFailure();
            return;
        }
        strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
        profileMatch = true;
        return;
    }

    File f = LittleFS.open(PATH, "r");
    if (!f) {
        Serial.println("[Config] Failed to open ecu_config.json");
        strncpy(EngineData::instance().faultDescription,
            "Cannot start: failed to open ecu_config.json.\n"
            "What to do: The config file may be missing or the filesystem is corrupt. "
            "Re-upload the filesystem image (pio run --target uploadfs) or use the web UI "
            "Tools page to reset config to defaults.",
            sizeof(EngineData::instance().faultDescription) - 1);
        EngineData::instance().faultDescription[sizeof(EngineData::instance().faultDescription) - 1] = '\0';
        profileMatch = false;
        return;
    }
    JsonDocument fullDoc;
    DeserializationError err = deserializeJson(fullDoc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
        strncpy(EngineData::instance().faultDescription,
            "Cannot start: ecu_config.json is corrupted (JSON parse error).\n"
            "What to do: Use the web UI Tools page to reset config to defaults, "
            "or re-upload the filesystem image.",
            sizeof(EngineData::instance().faultDescription) - 1);
        EngineData::instance().faultDescription[sizeof(EngineData::instance().faultDescription) - 1] = '\0';
        profileMatch = false;
        return;
    }

    // New unified format has a "settings" key; legacy flat format does not
    JsonDocument workDoc;
    if (fullDoc[SECTION].is<JsonObject>()) {
        workDoc.set(fullDoc[SECTION]);
    } else {
        // HardwareConfig may have created a unified file containing hardware
        // only. Do not interpret that document as settings with a blank profile.
        if (fullDoc["hardware"].is<JsonObject>()) {
            Serial.println("[Config] Settings missing from ecu_config.json - adding defaults");
            strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
            if (!save()) {
                inhibitStartForConfigWriteFailure();
                return;
            }
            profileMatch = true;
            return;
        }
        workDoc.set(fullDoc);   // legacy flat - re-save in new format next save()
    }

    const char* id = workDoc["profile_id"] | "";
    strncpy(profileId, id, sizeof(profileId) - 1);
    profileMatch = (strcmp(profileId, HardwareConfig::profileId) == 0);
    if (!profileMatch) {
        // Keep web repair available, but do not run with crossed engine sections.
        Serial.printf("[Config] WARNING: settings profile (%s) does not match hardware profile (%s)"
                      " - START inhibited until repaired\n",
                      profileId, HardwareConfig::profileId);
        _applyDefaults();
        strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
        inhibitStartForProfileMismatch();
        return;
    }

    uint8_t ver = workDoc["config_version"] | 0;
    if (ver != CONFIG_VERSION) {
        Serial.printf("[Config] Version mismatch (file=%u expected=%u) — new fields use defaults\n",
                      ver, CONFIG_VERSION);
        // Signal the web UI to show a calibration reminder banner
        EngineData::instance().configVersionMismatch = true;
    }

    bool settingsIncomplete = false;
    const char* requiredSections[] = {
        "engine", "oil", "sequence", "throttle", "safety", "calibration"
    };
    for (const char* section : requiredSections) {
        settingsIncomplete |= !workDoc[section].is<JsonObject>();
    }
    _missingRequiredSections = false;
    _fromDoc(workDoc);
    if (settingsIncomplete || _missingRequiredSections) {
        Serial.println("[Config] Completing missing settings sections in ecu_config.json");
        if (!save()) {
            inhibitStartForConfigWriteFailure();
            return;
        }
    }
    Serial.printf("[Config] Loaded OK — profile: %s\n", profileId);
}

volatile bool Config::_savePending = false;
volatile bool Config::_runtimeStatsSavePending = false;
bool Config::_missingRequiredSections = false;

bool Config::acquireStorageWrite() {
    if (!s_configWriteMutex) s_configWriteMutex = xSemaphoreCreateMutex();
    return s_configWriteMutex &&
           xSemaphoreTake(s_configWriteMutex, pdMS_TO_TICKS(2000)) == pdTRUE;
}

void Config::releaseStorageWrite() {
    if (s_configWriteMutex) xSemaphoreGive(s_configWriteMutex);
}

void Config::sanitizeForHardware() {
    if ((egtSource == 1 && !HardwareConfig::hasTot) ||
        (egtSource == 2 && !HardwareConfig::hasTit)) {
        egtSource = 0;
    }
    if ((!HardwareConfig::hasN1Rpm || !HardwareConfig::hasIgniter) && relightEnabled) {
        relightEnabled = false;
    }
    const bool hasN1 = HardwareConfig::hasN1Rpm;
    const bool hasN2 = HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm;
    if (!hasN1 && !hasN2) {
        standbyOilSource = 0;
    } else if (standbyOilSource == 0 && !hasN1) {
        standbyOilSource = 1;
    } else if (standbyOilSource == 1 && !hasN2) {
        standbyOilSource = 0;
    }
    if (!HardwareConfig::hasN1Rpm) sessionLogMask &= ~SLOG_N1;
    if (!HardwareConfig::hasTwoShaft || !HardwareConfig::hasN2Rpm) sessionLogMask &= ~SLOG_N2;
    if (!HardwareConfig::hasTot) sessionLogMask &= ~SLOG_TOT;
    if (!HardwareConfig::hasOilPress) sessionLogMask &= ~SLOG_OIL;
    if (!HardwareConfig::hasP1) sessionLogMask &= ~SLOG_P1;
    if (!HardwareConfig::hasP2) sessionLogMask &= ~SLOG_P2;
    if (!HardwareConfig::hasThrottle) sessionLogMask &= ~SLOG_THR;
    if (!HardwareConfig::hasTit) sessionLogMask &= ~SLOG_TIT;
    if (!HardwareConfig::hasBattVoltage) sessionLogMask &= ~SLOG_BATT;
    if (!HardwareConfig::hasFuelPress) sessionLogMask &= ~SLOG_FUEL_PRESS;
    if (!HardwareConfig::hasFuelFlow) sessionLogMask &= ~SLOG_FUEL_FLOW;
    if (!HardwareConfig::hasGlowPlug) sessionLogMask &= ~SLOG_GLOW;
    if (!HardwareConfig::hasFuelPump2) sessionLogMask &= ~SLOG_FP2;
    if (!HardwareConfig::hasAfterburner) sessionLogMask &= ~SLOG_AB;
    if (!HardwareConfig::hasPropPitch) sessionLogMask &= ~SLOG_PROP;
    if (!HardwareConfig::hasOilPump) sessionLogMask &= ~SLOG_OIL_PCT;

    int out = 0;
    for (int i = 0; i < ruleCount; i++) {
        Rule r = rules[i];
        if (!ruleSensorAvailable(r.sensor) || !ruleActuatorAvailable(r.actuator)) continue;
        r.onValue = constrain(r.onValue, -1.0f, 1.0f);
        r.offValue = constrain(r.offValue, -1.0f, 1.0f);
        rules[out++] = r;
    }
    for (int i = out; i < MAX_RULES; i++) rules[i] = {};
    ruleCount = out;
}

class ConfigStorageWriteRelease {
public:
    ~ConfigStorageWriteRelease() { Config::releaseStorageWrite(); }
};

void Config::requestSave() {
    // Called from Core 1 — sets a flag only, zero file I/O.
    // Core 0 picks this up in flushPendingSave() via WebServer::tick().
    _savePending = true;
}

bool Config::flushPendingSave() {
    if (!_savePending) return false;
    _savePending = false;
    bool ok = save();
    if (!ok) Serial.println("[Config] WARNING: deferred save failed — totalRunSeconds not persisted");
    return ok;
}

void Config::requestRuntimeStatsSave() {
    _runtimeStatsSavePending = true;
}

static void runtimeStatsKey(char* key, size_t len) {
    const char* profile = Config::profileId[0] ? Config::profileId : HardwareConfig::profileId;
    uint32_t hash = 2166136261u;
    for (const char* p = profile; p && *p; ++p) {
        hash ^= (uint8_t)*p;
        hash *= 16777619u;
    }
    snprintf(key, len, "run%08lx", (unsigned long)hash);
}

void Config::loadRuntimeStats() {
    char key[14];
    runtimeStatsKey(key, sizeof(key));
    Preferences stats;
    if (!stats.begin("ot", true)) return;
    uint32_t saved = stats.getUInt(key, totalRunSeconds);
    stats.end();
    if (saved > totalRunSeconds) totalRunSeconds = saved;
}

bool Config::flushPendingRuntimeStats() {
    if (!_runtimeStatsSavePending) return false;
    _runtimeStatsSavePending = false;
    char key[14];
    runtimeStatsKey(key, sizeof(key));
    Preferences stats;
    if (!stats.begin("ot", false)) {
        Serial.println("[Config] WARNING: failed to open NVS for accumulated runtime");
        return false;
    }
    size_t written = stats.putUInt(key, totalRunSeconds);
    stats.end();
    if (written == 0) {
        Serial.println("[Config] WARNING: accumulated runtime NVS write failed");
        return false;
    }
    return true;
}

int Config::effectiveEgtSource() {
    if (egtSource == 1 && HardwareConfig::hasTot) return 1;
    if (egtSource == 2 && HardwareConfig::hasTit) return 2;
    if (HardwareConfig::hasTot) return 1;
    if (HardwareConfig::hasTit) return 2;
    return 0;
}

bool Config::primaryEgtHealthy(const EngineData& ed) {
    switch (effectiveEgtSource()) {
        case 1: return ed.totHealthy;
        case 2: return ed.titHealthy;
        default: return false;
    }
}

float Config::primaryEgtC(const EngineData& ed) {
    switch (effectiveEgtSource()) {
        case 1: return ed.tot;
        case 2: return ed.tit;
        default: return 0.0f;
    }
}

float Config::primaryEgtLimitC() {
    switch (effectiveEgtSource()) {
        case 1: return totLimit;
        case 2: return titLimit;
        default: return 0.0f;
    }
}

const char* Config::primaryEgtLabel() {
    switch (effectiveEgtSource()) {
        case 1: return "TOT";
        case 2: return "TIT";
        default: return "EGT";
    }
}

void Config::clearRuntimeStats() {
    char key[14];
    runtimeStatsKey(key, sizeof(key));
    Preferences stats;
    if (!stats.begin("ot", false)) return;
    stats.remove(key);
    stats.end();
    totalRunSeconds = 0;
}

bool Config::save() {
    static constexpr const char* TMP_PATH = "/ecu_config.tmp";
    static constexpr const char* BAK_PATH = "/ecu_config.bak";
    if (!acquireStorageWrite()) {
        Serial.println("[Config] Timed out waiting to write ecu_config.json");
        return false;
    }
    ConfigStorageWriteRelease release;

    // Read-modify-write: preserve other sections (hardware etc.)
    JsonDocument fullDoc;
    File fr = LittleFS.open(PATH, "r");
    if (fr) {
        DeserializationError err = deserializeJson(fullDoc, fr);
        fr.close();
        if (err) {
            Serial.printf("[Config] Refusing to overwrite unreadable ecu_config.json: %s\n",
                          err.c_str());
            return false;
        }
    }

    JsonDocument settingsDoc;
    _toDoc(settingsDoc);
    fullDoc[SECTION].set(settingsDoc);

    // Write to temp file first — if power is lost mid-write the original is still intact.
    File fw = LittleFS.open(TMP_PATH, "w");
    if (!fw) { Serial.println("[Config] Failed to open ecu_config.tmp for write"); return false; }
    size_t expected = measureJsonPretty(fullDoc);
    size_t written = serializeJsonPretty(fullDoc, fw);
    fw.close();
    if (written != expected) {
        LittleFS.remove(TMP_PATH);
        Serial.println("[Config] Incomplete write to ecu_config.tmp");
        return false;
    }

    // Keep the previous valid file available for recovery until replacement succeeds.
    LittleFS.remove(BAK_PATH);
    bool hadOriginal = LittleFS.exists(PATH);
    if (hadOriginal && !LittleFS.rename(PATH, BAK_PATH)) {
        LittleFS.remove(TMP_PATH);
        Serial.println("[Config] failed to preserve previous ecu_config.json");
        return false;
    }
    if (!LittleFS.rename(TMP_PATH, PATH)) {
        Serial.println("[Config] rename ecu_config.tmp failed");
        if (hadOriginal) LittleFS.rename(BAK_PATH, PATH);
        return false;
    }
    if (hadOriginal) LittleFS.remove(BAK_PATH);
    return true;
}

bool Config::isLocked() {
    auto m = EngineData::instance().mode;
    return m == SysMode::STARTUP || m == SysMode::RUNNING || m == SysMode::SHUTDOWN;
}

size_t Config::toJson(char* buf, size_t len) {
    JsonDocument doc;
    _toDoc(doc);
    return serializeJson(doc, buf, len);
}

void Config::toJson(JsonDocument& doc) {
    _toDoc(doc);
}

bool Config::validateJson(const char* json, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return false;
    return validateJson(doc);
}

bool Config::validateJson(const JsonDocument& doc) {
    return validateSettingsDoc(doc);
}

bool Config::fromJson(const char* json, size_t len) {
    if (isLocked() || !validateJson(json, len)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    if (strcmp(doc["profile_id"] | "", HardwareConfig::profileId) != 0) return false;
    JsonDocument previous;
    _toDoc(previous);
    bool previousMismatch = EngineData::instance().configVersionMismatch;
    _fromDoc(doc);
    if (!save()) {
        _fromDoc(previous);
        EngineData::instance().configVersionMismatch = previousMismatch;
        return false;
    }
    profileMatch = true;
    EngineData::instance().configVersionMismatch = false;
    return true;
}

bool Config::fromJson(const JsonDocument& doc) {
    if (isLocked()) return false;
    const char* id = doc["profile_id"] | "";
    if (!id[0] || strcmp(id, HardwareConfig::profileId) != 0) return false;
    JsonDocument previous;
    _toDoc(previous);
    bool previousMismatch = EngineData::instance().configVersionMismatch;
    _fromDoc(doc);
    if (!save()) {
        _fromDoc(previous);
        EngineData::instance().configVersionMismatch = previousMismatch;
        return false;
    }
    profileMatch = true;
    EngineData::instance().configVersionMismatch = false;
    return true;
}

// ── Private helpers ───────────────────────────────────────────
void Config::_applyDefaults() {
    // Re-assign every field to its compile-time default so that load() is
    // idempotent: missing JSON keys restore to the default rather than
    // keeping a stale runtime value from a previous load() call.
    rpmLimit = 100000; minRpm = 30000; totLimit = 750;
    totCooldownTarget = 150; totSafeMargin = 50;
    oilStartupPressure = 2.5f; oilStartupPct = 80.0f; oilStartupMinBar = 1.5f;
    oilRunningMin = 2.8f; oilMapMin = 3.6f; oilMapMax = 4.4f;
    oilUseThrottleMap = false; oilAdjustScale = 1.80f; oilMinPct = 18.0f;
    oilFailsafeDelayMs = 1500; oilFailsafePct = 60.0f;
    startupOilArmTimeoutMs = 3000; preIgnRpm = 5000; preIgnSparkMs = 1500;
    flameTimeoutMs = 5000; flameCheckIntervalMs = 300; flameRequiredCount = 3;
    spoolRpmTarget = 32000; spoolTimeoutMs = 12000;
    safetyHoldMs = 1000; safetyHoldFinalRpm = 31000;
    starterDemand = 60.0f; starterTimeoutMs = 8000;
    tempConfirmTarget = 200.0f; tempConfirmTimeoutMs = 10000;
    waitForInputChannel = 0; waitForInputExpected = true; waitForInputTimeoutMs = 0;
    timedDelayMs = 1000; modifiedIdleMultiplier = 1.0f;
    fuelPulsePulseMs = 200; fuelPulseOffMs = 300;
    waitTotCoolTarget = 150.0f; waitTotCoolTimeoutMs = 120000;
    throttleSetPct = 10.0f; preHeatMs = 3000; oilPumpOnPct = 100.0f;
    flameConfirmTurnOffIgniter = true;
    safetyHoldTurnOffStarter = false; safetyHoldTurnOffStarterEn = false; safetyHoldTurnOffIgniter = false;
    spoolCutStarterOnExit = true; spoolCutStarterEnOnExit = true;
    hotStartTotThreshold = 0.0f; finalStopOilScavengeMs = 0;
    oilPrimeUseScavengePump = false; cooldownUseScavengePump = false;
    shutdownRpmDropThreshold = 5000; shutdownRpmDropTimeoutMs = 15000;
    shutdownCooldownTimeoutMs = 60000; shutdownFinalStopTimeoutMs = 10000;
    rpmZeroThreshold = 100.0f;
    cooldownUseStarter = true; cooldownUseOilPump = true;
    cooldownStarterPct = 40.0f; cooldownOilPct = 30.0f; cooldownOilPressureTarget = 2.0f;
    throttleRampUpMs = 600; throttleRampDownMs = 800;
    throttleIdleMinPct = 8; throttleIdleMaxPct = 18; throttleExpo = 0.0f;
    idleTargetRpm = 44000; idleRampUpMs = 10000; idleRampDownMs = 20000;
    idleDeadbandRpm = 300; idleRpmLimit = 60000; idleMinMultiplier = 0.75f;
    idleUseN2 = false; idleIGain = 0.0f; idleIMax = 0.10f;
    safetyCheckIntervalMs = 100; flameoutShutdownMs = 3000;
    egtSource = 0; flameoutSource = 0; flameoutN1MinRpm = 0.0f; flameoutTotDropC = 80.0f;
    totRiseRateLimitDegPerSec = 0.0f; titLimit = 0.0f; oilTempLimit = 120.0f;
    fuelPressMin = 0.0f; battVoltMin = 0.0f; surgeDetectRpmVariance = 0.0f;
    relightEnabled = false; relightConfirmSource = 0; relightMinRpm = 30000.0f;
    relightConfirmRpm = 0.0f; relightTotRiseC = 30.0f; relightTimeoutMs = 10000;
    toolFuelPrimeMs = 3000; toolOilPrimeMs = 5000; toolIgnTestMs = 2000;
    toolStartTestMs = 2000; toolFuelSolTestMs = 1000;
    wsIntervalMs = 333; snapshotIntervalMs = 10000; logStandby = false;
    starterAssistPct = 15.0f; starterAssistExitRpm = 1000.0f; starterRampPctPerSec = 10.0f;
    oilZeroBar = 0.1f; oilPressureDeadband = 0.2f;
    standbyOilSource = 0; standbyOilRpmLimit = 100.0f; standbyOilFeedPct = 25.0f;
    limpMaxThrottlePct = 50.0f; igniterOnStart = true;
    cooldownSkipHoldMs = 1000;
    fuelPumpIdleMinPct = 8.0f; fuelPumpIdleMaxPct = 18.0f;
    fp2StartPct = 0.0f; fp2EndPct = 80.0f; fp2RampMs = 3000; fp2DemandPct = 0.0f;
    govHoldTimeoutMs = 10000;
    abMinN1 = 30000.0f; abMaxN1 = 0.0f; abMaxTotForLight = 0.0f;
    abThrottleThreshold = 0.80f; abUseTorch = true; abUseIgniter = false;
    abTorchSpikePct = 30.0f; abTorchDurationMs = 400; abTorchTotLimit = 0.0f;
    abFlameMode = 2; abTotRiseDegC = 30.0f; abTotRiseWindowMs = 2000;
    abAssumeIgnitedMs = 1500; abFlameTimeoutMs = 3000;
    abPumpMinPct = 80.0f; abPumpMaxPct = 100.0f; abPumpControlMode = 0; abPumpFollowThrottle = false;
    abMainFuelOffsetPct = 0.0f; abStabilizeMs = 1000; abStabilizeMaxTot = 0.0f;
    rpmJumpThreshold = 0.40f; rpmZeroStuckTicks = 5;
    n1WarnRpm = 90000.0f; n2WarnRpm = 22000.0f; totWarnC = 0.0f; oilWarnBar = 0.0f;
    clusterEnabled = true; pressureSensorsEnabled = false;
    rcMinUs = 1000; rcMaxUs = 2000; rcFailsafeMs = 500;
    governorTargetRpm = 0.0f; governorBandRpm = 500.0f;
    governorKp = 0.001f; governorPitchKp = 0.0005f; governorPitchRampSec = 10.0f;
    propPitchIdleDeg = 15.0f; propPitchMaxDeg = 35.0f;
    glowPreheatMs = 10000; glowPreheatMaxPct = 80.0f; glowHoldPct = 30.0f; glowWaitUntilHot = false;
    throttleMinRaw = 0; throttleMaxRaw = 4095;
    idleMinRaw = 0; idleMaxRaw = 4095; flameThreshold = 500;
    oilPolyA = 0; oilPolyB = 0; oilPolyC = 0; oilPolyD = 0;
    oilPolyXMin = 0; oilPolyXMax = 4095;
    p1RawMin = 0; p1RawMax = 4095; p1ValMax = 10.0f; p1ZeroBar = 0.0f;
    p2RawMin = 0; p2RawMax = 4095; p2ValMax = 10.0f; p2ZeroBar = 0.0f;
    fuelPressRawMin = 0; fuelPressRawMax = 4095; fuelPressValMax = 10.0f;
    fuelFlowRawMin = 0; fuelFlowRawMax = 4095; fuelFlowValMax = 10.0f;
    sessionLogMask = SLOG_DEFAULT; sessionLogIntervalMs = 1000;
    // totalRunSeconds is NOT reset — hour meter persists across config reloads
}

void Config::_fromDoc(const JsonDocument& doc) {
    // Warn if an expected top-level section is entirely absent.
    // This typically means the file is truncated or severely corrupted —
    // individual missing fields within a section are normal during version upgrades
    // and are handled silently (they keep their compile-time defaults).
    const char* requiredSections[] = {
        "engine", "oil", "sequence", "throttle", "safety", "calibration"
    };
    for (const char* sec : requiredSections) {
        if (!doc[sec].is<JsonObject>()) {
            _missingRequiredSections = true;
            Serial.printf("[Config] WARNING: '%s' section missing from ecu_config.json"
                          " — affected fields will use compile-time defaults\n", sec);
        }
    }

    auto eng = doc["engine"];
    rpmLimit          = eng["rpm_limit"]          | rpmLimit;
    minRpm            = eng["min_rpm"]             | minRpm;
    totLimit          = eng["tot_limit"]           | totLimit;
    totCooldownTarget = eng["tot_cooldown_target"] | totCooldownTarget;
    totSafeMargin     = eng["tot_safe_margin"]     | totSafeMargin;

    auto oil = doc["oil"];
    oilStartupPressure = oil["startup_pressure"]   | oilStartupPressure;
    oilStartupPct      = oil["startup_pct"]        | oilStartupPct;
    oilStartupMinBar   = oil["startup_min_bar"]    | oilStartupMinBar;
    oilRunningMin      = oil["running_min"]        | oilRunningMin;
    oilMapMin          = oil["map_min"]            | oilMapMin;
    oilMapMax          = oil["map_max"]            | oilMapMax;
    if (!oil["use_throttle_map"].isNull()) oilUseThrottleMap = oil["use_throttle_map"].as<bool>();
    oilAdjustScale     = oil["adjust_scale"]       | oilAdjustScale;
    oilMinPct          = oil["min_pct"]            | oilMinPct;
    oilFailsafeDelayMs = oil["failsafe_delay_ms"]  | oilFailsafeDelayMs;
    oilFailsafePct     = oil["failsafe_pct"]       | oilFailsafePct;
    oilZeroBar         = oil["zero_bar"]           | oilZeroBar; // compatibility with older UI saves

    auto su = doc["sequence"]["startup"];
    startupOilArmTimeoutMs = su["oil_arm_timeout_ms"]      | startupOilArmTimeoutMs;
    preIgnRpm              = su["pre_ign_rpm"]             | preIgnRpm;
    preIgnSparkMs          = su["pre_ign_spark_ms"]        | preIgnSparkMs;
    flameTimeoutMs         = su["flame_timeout_ms"]        | flameTimeoutMs;
    flameCheckIntervalMs   = su["flame_check_interval_ms"] | flameCheckIntervalMs;
    flameRequiredCount     = su["flame_required_count"]    | flameRequiredCount;
    spoolRpmTarget         = su["rpm_target"]              | spoolRpmTarget;
    spoolTimeoutMs         = su["rpm_timeout_ms"]          | spoolTimeoutMs;
    safetyHoldMs           = su["safety_hold_ms"]          | safetyHoldMs;
    safetyHoldFinalRpm     = su["final_check_rpm"]         | safetyHoldFinalRpm;
    starterDemand          = su["starter_demand"]          | starterDemand;
    starterTimeoutMs       = su["starter_timeout_ms"]      | starterTimeoutMs;
    tempConfirmTarget      = su["temp_confirm_target"]     | tempConfirmTarget;
    tempConfirmTimeoutMs   = su["temp_confirm_timeout"]    | tempConfirmTimeoutMs;
    waitForInputChannel    = su["wait_for_input_ch"]       | waitForInputChannel;
    waitForInputTimeoutMs  = su["wait_for_input_timeout"]  | waitForInputTimeoutMs;
    if (!su["wait_for_input_state"].isNull()) waitForInputExpected = su["wait_for_input_state"].as<bool>();
    timedDelayMs           = su["timed_delay_ms"]          | timedDelayMs;
    modifiedIdleMultiplier = su["modified_idle_multiplier"]| modifiedIdleMultiplier;
    fuelPulsePulseMs       = su["fuel_pulse_ms"]           | fuelPulsePulseMs;
    fuelPulseOffMs         = su["fuel_off_ms"]             | fuelPulseOffMs;
    waitTotCoolTarget      = su["wait_tot_target"]         | waitTotCoolTarget;
    waitTotCoolTimeoutMs   = su["wait_tot_timeout"]        | waitTotCoolTimeoutMs;
    throttleSetPct         = su["throttle_set_pct"]        | throttleSetPct;
    preHeatMs              = su["preheat_ms"]              | preHeatMs;
    oilPumpOnPct           = su["oil_pump_on_pct"]         | oilPumpOnPct;
    if (!su["flame_turn_off_igniter"].isNull())    flameConfirmTurnOffIgniter  = su["flame_turn_off_igniter"].as<bool>();
    if (!su["safety_turn_off_starter"].isNull())   safetyHoldTurnOffStarter    = su["safety_turn_off_starter"].as<bool>();
    if (!su["safety_turn_off_starter_en"].isNull())safetyHoldTurnOffStarterEn  = su["safety_turn_off_starter_en"].as<bool>();
    if (!su["safety_turn_off_igniter"].isNull())   safetyHoldTurnOffIgniter    = su["safety_turn_off_igniter"].as<bool>();
    if (!su["spool_cut_starter_on_exit"].isNull()) spoolCutStarterOnExit    = su["spool_cut_starter_on_exit"].as<bool>();
    if (!su["spool_cut_starter_en_on_exit"].isNull()) spoolCutStarterEnOnExit = su["spool_cut_starter_en_on_exit"].as<bool>();
    hotStartTotThreshold = su["hot_start_tot_threshold"] | hotStartTotThreshold;
    if (!su["oil_prime_use_scavenge"].isNull()) oilPrimeUseScavengePump = su["oil_prime_use_scavenge"].as<bool>();
    fp2StartPct        = su["fp2_start_pct"]        | fp2StartPct;
    fp2EndPct          = su["fp2_end_pct"]          | fp2EndPct;
    fp2RampMs          = su["fp2_ramp_ms"]          | fp2RampMs;
    fp2DemandPct       = su["fp2_demand_pct"]       | fp2DemandPct;
    govHoldTimeoutMs   = su["gov_hold_timeout_ms"]  | govHoldTimeoutMs;

    auto sd = doc["sequence"]["shutdown"];
    shutdownRpmDropThreshold   = sd["rpm_drop_threshold"]    | shutdownRpmDropThreshold;
    shutdownRpmDropTimeoutMs   = sd["rpm_drop_timeout_ms"]   | shutdownRpmDropTimeoutMs;
    shutdownCooldownTimeoutMs  = sd["cooldown_timeout_ms"]   | shutdownCooldownTimeoutMs;
    shutdownFinalStopTimeoutMs = sd["final_stop_timeout_ms"]  | shutdownFinalStopTimeoutMs;
    finalStopOilScavengeMs     = sd["oil_scavenge_ms"]        | finalStopOilScavengeMs;
    if (!sd["cooldown_use_scavenge"].isNull()) cooldownUseScavengePump = sd["cooldown_use_scavenge"].as<bool>();
    if (!sd["cooldown_use_starter"].isNull()) cooldownUseStarter = sd["cooldown_use_starter"].as<bool>();
    if (!sd["cooldown_use_oil"].isNull())     cooldownUseOilPump = sd["cooldown_use_oil"].as<bool>();
    cooldownStarterPct        = sd["cooldown_starter_pct"]        | cooldownStarterPct;
    cooldownOilPct            = sd["cooldown_oil_pct"]            | cooldownOilPct;
    cooldownOilPressureTarget = sd["cooldown_oil_pressure_bar"]   | cooldownOilPressureTarget;
    rpmZeroThreshold          = sd["rpm_zero_threshold"]          | rpmZeroThreshold;

    auto th = doc["throttle"];
    throttleRampUpMs    = th["ramp_up_ms"]   | throttleRampUpMs;
    throttleRampDownMs  = th["ramp_down_ms"] | throttleRampDownMs;
    throttleIdleMinPct  = th["idle_min_pct"] | throttleIdleMinPct;
    throttleIdleMaxPct  = th["idle_max_pct"] | throttleIdleMaxPct;
    throttleExpo        = th["expo"]         | throttleExpo;

    auto di = doc["dynamic_idle"];
    idleTargetRpm    = di["target_rpm"]    | idleTargetRpm;
    idleRampUpMs     = di["ramp_up_ms"]    | idleRampUpMs;
    idleRampDownMs   = di["ramp_down_ms"]  | idleRampDownMs;
    idleDeadbandRpm  = di["deadband_rpm"]  | idleDeadbandRpm;
    idleRpmLimit     = di["rpm_limit"]     | idleRpmLimit;
    idleMinMultiplier= di["min_multiplier"]| idleMinMultiplier;
    if (!di["use_n2"].isNull()) idleUseN2 = di["use_n2"].as<bool>();
    idleIGain        = di["i_gain"]        | idleIGain;
    idleIMax         = di["i_max"]         | idleIMax;

    auto sf = doc["safety"];
    safetyCheckIntervalMs         = sf["check_interval_ms"]         | safetyCheckIntervalMs;
    flameoutShutdownMs            = sf["flameout_shutdown_ms"]       | flameoutShutdownMs;
    if (sf["egt_source"].isNull()) _missingRequiredSections = true;
    egtSource                     = sf["egt_source"]                 | egtSource;
    flameoutSource                = sf["flameout_source"]            | flameoutSource;
    flameoutN1MinRpm              = sf["flameout_n1_min_rpm"]        | flameoutN1MinRpm;
    flameoutTotDropC              = sf["flameout_tot_drop_c"]        | flameoutTotDropC;
    totRiseRateLimitDegPerSec     = sf["tot_rise_rate_limit_deg_s"]  | totRiseRateLimitDegPerSec;
    titLimit                      = sf["tit_limit_c"]                | titLimit;
    oilTempLimit                  = sf["oil_temp_limit_c"]           | oilTempLimit;
    fuelPressMin                  = sf["fuel_press_min_bar"]         | fuelPressMin;
    battVoltMin                   = sf["batt_volt_min_v"]            | battVoltMin;
    surgeDetectRpmVariance        = sf["surge_detect_rpm_variance"]  | surgeDetectRpmVariance;
    if (egtSource < 0 || egtSource > 2) egtSource = 0;

    auto gov = doc["governor"];
    governorTargetRpm  = gov["target_rpm"]   | governorTargetRpm;
    governorBandRpm    = gov["band_rpm"]      | governorBandRpm;
    governorKp         = gov["kp"]            | governorKp;
    governorPitchKp      = gov["pitch_kp"]         | governorPitchKp;
    governorPitchRampSec = gov["pitch_ramp_sec"]   | governorPitchRampSec;
    propPitchIdleDeg   = gov["pitch_idle_deg"]| propPitchIdleDeg;
    propPitchMaxDeg    = gov["pitch_max_deg"] | propPitchMaxDeg;

    auto glw = doc["glow_plug"];
    glowPreheatMs      = glw["preheat_ms"]    | glowPreheatMs;
    glowPreheatMaxPct  = glw["preheat_max_pct"]| glowPreheatMaxPct;
    glowHoldPct        = glw["hold_pct"]      | glowHoldPct;
    if (!glw["wait_until_hot"].isNull()) glowWaitUntilHot = glw["wait_until_hot"].as<bool>();

    auto cal = doc["calibration"];
    throttleMinRaw = cal["throttle_min_raw"] | throttleMinRaw;
    throttleMaxRaw = cal["throttle_max_raw"] | throttleMaxRaw;
    idleMinRaw     = cal["idle_min_raw"]     | idleMinRaw;
    idleMaxRaw     = cal["idle_max_raw"]     | idleMaxRaw;
    flameThreshold = cal["flame_threshold"]  | flameThreshold;

    auto poly = cal["oil_poly"];
    oilPolyA    = poly["a"]     | oilPolyA;
    oilPolyB    = poly["b"]     | oilPolyB;
    oilPolyC    = poly["c"]     | oilPolyC;
    oilPolyD    = poly["d"]     | oilPolyD;
    oilPolyXMin = poly["x_min"] | oilPolyXMin;
    oilPolyXMax = poly["x_max"] | oilPolyXMax;
    p1RawMin         = cal["p1_raw_min"]         | p1RawMin;
    p1RawMax         = cal["p1_raw_max"]         | p1RawMax;
    p1ValMax         = cal["p1_val_max"]         | p1ValMax;
    p2RawMin         = cal["p2_raw_min"]         | p2RawMin;
    p2RawMax         = cal["p2_raw_max"]         | p2RawMax;
    p2ValMax         = cal["p2_val_max"]         | p2ValMax;
    p1ZeroBar        = cal["p1_zero_bar"]        | p1ZeroBar;
    p2ZeroBar        = cal["p2_zero_bar"]        | p2ZeroBar;
    fuelPressRawMin  = cal["fuel_press_raw_min"] | fuelPressRawMin;
    fuelPressRawMax  = cal["fuel_press_raw_max"] | fuelPressRawMax;
    fuelPressValMax  = cal["fuel_press_val_max"] | fuelPressValMax;
    fuelFlowRawMin   = cal["fuel_flow_raw_min"]  | fuelFlowRawMin;
    fuelFlowRawMax   = cal["fuel_flow_raw_max"]  | fuelFlowRawMax;
    fuelFlowValMax   = cal["fuel_flow_val_max"]  | fuelFlowValMax;

    auto rl = doc["relight"];
    if (!rl["enabled"].isNull()) relightEnabled = rl["enabled"].as<bool>();
    relightConfirmSource = rl["confirm_source"]   | relightConfirmSource;
    relightMinRpm     = rl["min_rpm"]          | relightMinRpm;
    relightConfirmRpm = rl["confirm_rpm"]      | relightConfirmRpm;
    relightTotRiseC   = rl["tot_rise_c"]       | relightTotRiseC;
    relightTimeoutMs  = rl["relight_timeout_ms"] | relightTimeoutMs;

    auto tl = doc["tools"];
    toolFuelPrimeMs   = tl["fuel_prime_ms"]   | toolFuelPrimeMs;
    toolOilPrimeMs    = tl["oil_prime_ms"]    | toolOilPrimeMs;
    toolIgnTestMs     = tl["ign_test_ms"]     | toolIgnTestMs;
    toolStartTestMs   = tl["start_test_ms"]   | toolStartTestMs;
    toolFuelSolTestMs = tl["fuel_sol_test_ms"]| toolFuelSolTestMs;

    auto tm = doc["telemetry"];
    wsIntervalMs       = tm["ws_interval_ms"]       | wsIntervalMs;
    snapshotIntervalMs = tm["snapshot_interval_ms"] | snapshotIntervalMs;
    if (!tm["log_standby"].isNull()) logStandby = tm["log_standby"].as<bool>();

    auto sa = doc["starter_assist"];
    starterAssistPct      = sa["pct"]           | starterAssistPct;
    starterAssistExitRpm  = sa["exit_rpm"]      | starterAssistExitRpm;
    starterRampPctPerSec  = sa["ramp_pct_per_s"]| starterRampPctPerSec;

    auto oilx = doc["oil_advanced"];
    oilZeroBar          = oilx["zero_bar"]      | oilZeroBar;
    oilPressureDeadband = oilx["deadband_bar"]  | oilPressureDeadband;

    // ignition section kept for forward compatibility but postIgnDwellMs removed

    auto sob = doc["standby_oil"];
    standbyOilSource   = sob["source"]    | standbyOilSource;
    standbyOilRpmLimit = sob["rpm_limit"] | standbyOilRpmLimit;
    standbyOilFeedPct  = sob["feed_pct"]  | standbyOilFeedPct;

    auto limp = doc["limp_mode"];
    limpMaxThrottlePct = limp["max_throttle_pct"] | limpMaxThrottlePct;

    auto misc = doc["misc"];
    cooldownSkipHoldMs = misc["cooldown_skip_hold_ms"] | cooldownSkipHoldMs;
    if (!misc["igniter_on_start"].isNull()) igniterOnStart = misc["igniter_on_start"].as<bool>();

    auto fp = doc["fuel_pump"];
    fuelPumpIdleMinPct = fp["idle_min_pct"] | fuelPumpIdleMinPct;
    fuelPumpIdleMaxPct = fp["idle_max_pct"] | fuelPumpIdleMaxPct;

    auto rh = doc["rpm_health"];
    rpmJumpThreshold  = rh["jump_threshold"]   | rpmJumpThreshold;
    rpmZeroStuckTicks = rh["zero_stuck_ticks"] | rpmZeroStuckTicks;

    auto cl = doc["cluster"];
    n1WarnRpm      = cl["n1_warn_rpm"]  | n1WarnRpm;
    n2WarnRpm      = cl["n2_warn_rpm"]  | n2WarnRpm;
    totWarnC       = cl["tot_warn_c"]   | totWarnC;
    oilWarnBar     = cl["oil_warn_bar"] | oilWarnBar;
    if (!cl["enabled"].isNull()) clusterEnabled = cl["enabled"].as<bool>();

    auto disp = doc["display"];
    if (!disp["pressure_sensors"].isNull()) pressureSensorsEnabled = disp["pressure_sensors"].as<bool>();

    auto rc = doc["rc_input"];
    if (!rc["min_us"].isNull())        rcMinUs      = rc["min_us"].as<int>();
    if (!rc["max_us"].isNull())        rcMaxUs      = rc["max_us"].as<int>();
    if (!rc["failsafe_ms"].isNull())   rcFailsafeMs = rc["failsafe_ms"].as<int>();

    auto ab = doc["afterburner"];
    abMinN1              = ab["min_n1"]             | abMinN1;
    abMaxN1              = ab["max_n1"]             | abMaxN1;
    abMaxTotForLight     = ab["max_tot_for_light"]  | abMaxTotForLight;
    abThrottleThreshold  = ab["throttle_threshold"] | abThrottleThreshold;
    if (!ab["use_torch"].isNull())          abUseTorch           = ab["use_torch"].as<bool>();
    if (!ab["use_igniter"].isNull())        abUseIgniter         = ab["use_igniter"].as<bool>();
    abTorchSpikePct      = ab["torch_spike_pct"]    | abTorchSpikePct;
    abTorchDurationMs    = ab["torch_duration_ms"]  | abTorchDurationMs;
    abTorchTotLimit      = ab["torch_tot_limit"]    | abTorchTotLimit;
    abFlameMode          = ab["flame_mode"]          | abFlameMode;
    abTotRiseDegC        = ab["tot_rise_deg_c"]     | abTotRiseDegC;
    abTotRiseWindowMs    = ab["tot_rise_window_ms"] | abTotRiseWindowMs;
    abAssumeIgnitedMs    = ab["assume_ignited_ms"]  | abAssumeIgnitedMs;
    abFlameTimeoutMs     = ab["flame_timeout_ms"]   | abFlameTimeoutMs;
    abPumpMinPct         = ab["pump_min_pct"]        | abPumpMinPct;
    abPumpMaxPct         = ab["pump_max_pct"]        | abPumpMaxPct;
    if (!ab["pump_control_mode"].isNull()) {
        abPumpControlMode = ab["pump_control_mode"].as<int>();
    } else if (!ab["pump_follow_throttle"].isNull()) {
        abPumpControlMode = ab["pump_follow_throttle"].as<bool>() ? 1 : 0;
    }
    abPumpFollowThrottle = (abPumpControlMode == 1);
    abMainFuelOffsetPct  = ab["main_fuel_offset_pct"]| abMainFuelOffsetPct;
    abStabilizeMs        = ab["stabilize_ms"]        | abStabilizeMs;
    abStabilizeMaxTot    = ab["stabilize_max_tot"]   | abStabilizeMaxTot;

    // Session log mask stored as individual bools in JSON
    auto sl = doc["session_log"];
    if (!sl.isNull()) {
        uint32_t mask = 0;
        if (sl["n1"]       | false) mask |= SLOG_N1;
        if (sl["n2"]       | false) mask |= SLOG_N2;
        if (sl["tot"]      | false) mask |= SLOG_TOT;
        if (sl["oil"]      | false) mask |= SLOG_OIL;
        if (sl["p1"]       | false) mask |= SLOG_P1;
        if (sl["p2"]       | false) mask |= SLOG_P2;
        if (sl["throttle"]   | false) mask |= SLOG_THR;
        if (sl["mode"]       | false) mask |= SLOG_MODE;
        if (sl["tit"]        | false) mask |= SLOG_TIT;
        if (sl["batt"]       | false) mask |= SLOG_BATT;
        if (sl["fuel_press"] | false) mask |= SLOG_FUEL_PRESS;
        if (sl["fuel_flow"]  | false) mask |= SLOG_FUEL_FLOW;
        if (sl["glow"]       | false) mask |= SLOG_GLOW;
        if (sl["fp2"]        | false) mask |= SLOG_FP2;
        if (sl["ab"]         | false) mask |= SLOG_AB;
        if (sl["prop"]       | false) mask |= SLOG_PROP;
        if (sl["oil_pct"]    | false) mask |= SLOG_OIL_PCT;
        sessionLogMask = mask;
        sessionLogIntervalMs = sl["interval_ms"] | sessionLogIntervalMs;
    }

    auto stats = doc["stats"];
    if (!stats.isNull()) {
        uint32_t fileRunSeconds = stats["total_run_seconds"] | totalRunSeconds;
        if (fileRunSeconds > totalRunSeconds) totalRunSeconds = fileRunSeconds;
    }

    // ── Automation rules ──────────────────────────────────────────
    auto rulesArr = doc["rules"];
    if (!rulesArr.isNull() && rulesArr.is<JsonArrayConst>()) {
        ruleCount = 0;
        for (JsonObjectConst jr : rulesArr.as<JsonArrayConst>()) {
            if (ruleCount >= MAX_RULES) break;
            Rule& r = rules[ruleCount++];
            r.enabled   = jr["enabled"]   | false;
            r.sensor    = (uint8_t)(jr["sensor"]    | 0);
            r.op        = (uint8_t)(jr["op"]        | 0);
            r.threshold = jr["threshold"] | 0.0f;
            r.actuator  = (uint8_t)(jr["actuator"]  | 0);
            r.onValue   = jr["on_value"]  | 1.0f;
            r.offValue  = jr["off_value"] | 0.0f;
            const char* n = jr["name"] | "";
            strncpy(r.name, n, sizeof(r.name) - 1);
            r.name[sizeof(r.name) - 1] = '\0';
        }
    }

    if (rpmLimit <= 0.0f) rpmLimit = 100000.0f;
    if (minRpm <= 0.0f) minRpm = 30000.0f;
    if (minRpm >= rpmLimit) minRpm = rpmLimit * 0.3f;
    if (totLimit <= 0.0f) totLimit = 750.0f;
    if (totCooldownTarget < 0.0f) totCooldownTarget = 0.0f;
    if (totCooldownTarget >= totLimit) totCooldownTarget = totLimit * 0.2f;
    totSafeMargin = constrain(totSafeMargin, 0.0f, totLimit);
    if (oilStartupMinBar < 0.0f) oilStartupMinBar = 1.5f;
    if (oilRunningMin < 0.0f) oilRunningMin = 2.8f;
    if (oilStartupPressure < 0.0f) oilStartupPressure = 0.0f;
    if (oilMapMin < 0.0f) oilMapMin = 0.0f;
    if (oilMapMax < oilMapMin) oilMapMax = oilMapMin;
    if (cooldownOilPressureTarget < 0.0f) cooldownOilPressureTarget = 0.0f;
    oilStartupPct = constrain(oilStartupPct, 0.0f, 100.0f);
    oilMinPct = constrain(oilMinPct, 0.0f, 100.0f);
    if (oilAdjustScale < 0.0f) oilAdjustScale = 0.0f;
    if (oilZeroBar < 0.0f) oilZeroBar = 0.0f;
    if (oilPressureDeadband < 0.0f) oilPressureDeadband = 0.0f;
    if (safetyCheckIntervalMs < 10) safetyCheckIntervalMs = 10;
    if (flameoutShutdownMs < 100.0f) flameoutShutdownMs = 100.0f;
    flameoutSource = constrain(flameoutSource, 0, 3);
    if (flameoutN1MinRpm < 0.0f) flameoutN1MinRpm = 0.0f;
    if (flameoutTotDropC < 0.0f) flameoutTotDropC = 0.0f;
    if (preIgnRpm < 0.0f) preIgnRpm = 0.0f;
    if (spoolRpmTarget < 0.0f) spoolRpmTarget = 0.0f;
    if (safetyHoldFinalRpm < 0.0f) safetyHoldFinalRpm = 0.0f;
    if (waitTotCoolTarget < 0.0f) waitTotCoolTarget = 0.0f;
    if (shutdownRpmDropThreshold < 0.0f) shutdownRpmDropThreshold = 0.0f;
    if (rpmZeroThreshold < 0.0f) rpmZeroThreshold = 0.0f;
    if (hotStartTotThreshold < 0.0f) hotStartTotThreshold = 0.0f;
    if (startupOilArmTimeoutMs < 0) startupOilArmTimeoutMs = 0;
    if (starterTimeoutMs < 0) starterTimeoutMs = 0;
    if (preIgnSparkMs < 0) preIgnSparkMs = 0;
    if (flameTimeoutMs < 0) flameTimeoutMs = 0;
    if (flameCheckIntervalMs < 1) flameCheckIntervalMs = 1;
    if (flameRequiredCount < 1) flameRequiredCount = 1;
    if (tempConfirmTimeoutMs < 0) tempConfirmTimeoutMs = 0;
    if (spoolTimeoutMs < 0) spoolTimeoutMs = 0;
    if (safetyHoldMs < 0) safetyHoldMs = 0;
    if (waitForInputTimeoutMs < 0) waitForInputTimeoutMs = 0;
    if (timedDelayMs < 0) timedDelayMs = 0;
    if (fuelPulsePulseMs < 0) fuelPulsePulseMs = 0;
    if (fuelPulseOffMs < 0) fuelPulseOffMs = 0;
    if (waitTotCoolTimeoutMs < 0) waitTotCoolTimeoutMs = 0;
    if (preHeatMs < 0) preHeatMs = 0;
    if (finalStopOilScavengeMs < 0) finalStopOilScavengeMs = 0;
    if (shutdownRpmDropTimeoutMs < 0) shutdownRpmDropTimeoutMs = 0;
    if (shutdownCooldownTimeoutMs < 0) shutdownCooldownTimeoutMs = 0;
    if (shutdownFinalStopTimeoutMs < 0) shutdownFinalStopTimeoutMs = 0;
    if (throttleRampUpMs < 0.0f) throttleRampUpMs = 0.0f;
    if (throttleRampDownMs < 0.0f) throttleRampDownMs = 0.0f;
    if (idleRampUpMs < 0.0f) idleRampUpMs = 0.0f;
    if (idleRampDownMs < 0.0f) idleRampDownMs = 0.0f;
    if (glowPreheatMs < 0) glowPreheatMs = 0;
    if (relightTimeoutMs < 0) relightTimeoutMs = 0;
    relightConfirmSource = constrain(relightConfirmSource, 0, 3);
    if (relightMinRpm < 0.0f) relightMinRpm = 0.0f;
    if (relightConfirmRpm < 0.0f) relightConfirmRpm = 0.0f;
    if (relightTotRiseC < 0.0f) relightTotRiseC = 0.0f;
    if (starterAssistExitRpm < 0.0f) starterAssistExitRpm = 0.0f;
    if (starterRampPctPerSec < 0.0f) starterRampPctPerSec = 0.0f;
    standbyOilSource = constrain(standbyOilSource, 0, 2);
    if (standbyOilRpmLimit < 0.0f) standbyOilRpmLimit = 0.0f;
    if (toolFuelPrimeMs > 60000u) toolFuelPrimeMs = 3000u;
    if (toolOilPrimeMs > 60000u) toolOilPrimeMs = 5000u;
    if (toolIgnTestMs > 60000u) toolIgnTestMs = 2000u;
    if (toolStartTestMs > 60000u) toolStartTestMs = 2000u;
    if (toolFuelSolTestMs > 60000u) toolFuelSolTestMs = 1000u;
    if (wsIntervalMs < 333u || wsIntervalMs > 60000u) wsIntervalMs = 333u;
    if (snapshotIntervalMs < 500u || snapshotIntervalMs > 3600000u) snapshotIntervalMs = 10000u;
    if (sessionLogIntervalMs < 100u || sessionLogIntervalMs > 60000u) sessionLogIntervalMs = 500u;
    if (cooldownSkipHoldMs < 0) cooldownSkipHoldMs = 0;
    if (fp2RampMs < 0) fp2RampMs = 0;
    if (govHoldTimeoutMs < 0) govHoldTimeoutMs = 0;
    starterDemand = constrain(starterDemand, 0.0f, 100.0f);
    throttleSetPct = constrain(throttleSetPct, 0.0f, 100.0f);
    oilPumpOnPct = constrain(oilPumpOnPct, 0.0f, 100.0f);
    cooldownStarterPct = constrain(cooldownStarterPct, 0.0f, 100.0f);
    cooldownOilPct = constrain(cooldownOilPct, 0.0f, 100.0f);
    throttleIdleMinPct = constrain(throttleIdleMinPct, 0.0f, 100.0f);
    throttleIdleMaxPct = constrain(throttleIdleMaxPct, throttleIdleMinPct, 100.0f);
    throttleExpo = constrain(throttleExpo, 0.0f, 1.0f);
    if (idleTargetRpm < 0.0f) idleTargetRpm = 0.0f;
    if (idleDeadbandRpm < 0.0f) idleDeadbandRpm = 0.0f;
    if (idleRpmLimit < 0.0f) idleRpmLimit = 0.0f;
    idleMinMultiplier = constrain(idleMinMultiplier, 0.0f, 1.0f);
    idleIGain = constrain(idleIGain, 0.0f, 2.0f);
    idleIMax = constrain(idleIMax, 0.0f, 0.5f);
    glowPreheatMaxPct = constrain(glowPreheatMaxPct, 0.0f, 100.0f);
    glowHoldPct = constrain(glowHoldPct, 0.0f, 100.0f);
    starterAssistPct = constrain(starterAssistPct, 0.0f, 100.0f);
    standbyOilFeedPct = constrain(standbyOilFeedPct, 0.0f, 100.0f);
    fuelPumpIdleMinPct = constrain(fuelPumpIdleMinPct, 0.0f, 100.0f);
    fuelPumpIdleMaxPct = constrain(fuelPumpIdleMaxPct, fuelPumpIdleMinPct, 100.0f);
    if (modifiedIdleMultiplier < 0.0f) modifiedIdleMultiplier = 0.0f;
    fp2StartPct = constrain(fp2StartPct, 0.0f, 100.0f);
    fp2EndPct = constrain(fp2EndPct, 0.0f, 100.0f);
    fp2DemandPct = constrain(fp2DemandPct, 0.0f, 100.0f);
    if (oilFailsafeDelayMs < 0) oilFailsafeDelayMs = 0;
    oilFailsafePct = constrain(oilFailsafePct, 0.0f, 100.0f);
    if (totRiseRateLimitDegPerSec < 0.0f) totRiseRateLimitDegPerSec = 0.0f;
    if (titLimit < 0.0f) titLimit = 0.0f;
    if (oilTempLimit < 0.0f) oilTempLimit = 0.0f;
    if (fuelPressMin < 0.0f) fuelPressMin = 0.0f;
    if (battVoltMin < 0.0f) battVoltMin = 0.0f;
    if (surgeDetectRpmVariance < 0.0f) surgeDetectRpmVariance = 0.0f;
    if (rpmZeroStuckTicks < 1) rpmZeroStuckTicks = 1;
    if (rcMinUs >= rcMaxUs) { rcMinUs = 1000; rcMaxUs = 2000; }
    if (rcFailsafeMs < 20) rcFailsafeMs = 500;
    if (throttleMinRaw == throttleMaxRaw) { throttleMinRaw = 0; throttleMaxRaw = 4095; }
    if (idleMinRaw == idleMaxRaw) { idleMinRaw = 0; idleMaxRaw = 4095; }
    oilPolyXMin = constrain(oilPolyXMin, 0.0f, 4095.0f);
    oilPolyXMax = constrain(oilPolyXMax, 0.0f, 4095.0f);
    if (oilPolyXMax <= oilPolyXMin) { oilPolyXMin = 0.0f; oilPolyXMax = 4095.0f; }
    auto sanitizeLinearCal = [](int& rawMin, int& rawMax, float& valMax) {
        rawMin = constrain(rawMin, 0, 4095);
        rawMax = constrain(rawMax, 0, 4095);
        if (rawMax <= rawMin) { rawMin = 0; rawMax = 4095; }
        if (valMax <= 0.0f) valMax = 10.0f;
    };
    sanitizeLinearCal(p1RawMin, p1RawMax, p1ValMax);
    sanitizeLinearCal(p2RawMin, p2RawMax, p2ValMax);
    sanitizeLinearCal(fuelPressRawMin, fuelPressRawMax, fuelPressValMax);
    sanitizeLinearCal(fuelFlowRawMin, fuelFlowRawMax, fuelFlowValMax);
    if (abTorchDurationMs < 0) abTorchDurationMs = 0;
    if (abMinN1 < 0.0f) abMinN1 = 0.0f;
    if (abMaxN1 < 0.0f) abMaxN1 = 0.0f;
    if (abMaxTotForLight < 0.0f) abMaxTotForLight = 0.0f;
    if (abTorchTotLimit < 0.0f) abTorchTotLimit = 0.0f;
    if (abFlameMode < 0 || abFlameMode > 2) abFlameMode = 2;
    if (abTotRiseDegC < 0.0f) abTotRiseDegC = 0.0f;
    if (abTotRiseWindowMs < 0) abTotRiseWindowMs = 0;
    if (abAssumeIgnitedMs < 0) abAssumeIgnitedMs = 0;
    if (abFlameTimeoutMs < abAssumeIgnitedMs) abFlameTimeoutMs = abAssumeIgnitedMs;
    if (abStabilizeMs < 0) abStabilizeMs = 0;
    if (abStabilizeMaxTot < 0.0f) abStabilizeMaxTot = 0.0f;
    abThrottleThreshold = constrain(abThrottleThreshold, 0.0f, 1.0f);
    abTorchSpikePct = constrain(abTorchSpikePct, 0.0f, 100.0f);
    abPumpMinPct = constrain(abPumpMinPct, 0.0f, 100.0f);
    abPumpMaxPct = constrain(abPumpMaxPct, abPumpMinPct, 100.0f);
    abPumpControlMode = constrain(abPumpControlMode, 0, 2);
    abPumpFollowThrottle = (abPumpControlMode == 1);
    abMainFuelOffsetPct = constrain(abMainFuelOffsetPct, -20.0f, 50.0f);
    limpMaxThrottlePct = constrain(limpMaxThrottlePct, 0.0f, 100.0f);
    governorKp = constrain(governorKp, 0.0f, 0.01f);
    governorPitchKp = constrain(governorPitchKp, 0.0f, 0.01f);
    if (governorTargetRpm < 0.0f) governorTargetRpm = 0.0f;
    if (governorBandRpm < 0.0f) governorBandRpm = 0.0f;
    if (propPitchIdleDeg < 0.0f) propPitchIdleDeg = 0.0f;
    if (propPitchMaxDeg < propPitchIdleDeg) propPitchMaxDeg = propPitchIdleDeg;
    if (governorPitchRampSec <= 0.0f) governorPitchRampSec = 1.0f;
    sanitizeForHardware();
}

void Config::_toDoc(JsonDocument& doc) {
    sanitizeForHardware();
    doc["profile_id"]     = HardwareConfig::profileId[0] ? HardwareConfig::profileId : OT_PROFILE_ID;
    doc["config_version"] = CONFIG_VERSION;

    auto eng = doc["engine"].to<JsonObject>();
    eng["rpm_limit"]          = rpmLimit;
    eng["min_rpm"]            = minRpm;
    eng["tot_limit"]          = totLimit;
    eng["tot_cooldown_target"]= totCooldownTarget;
    eng["tot_safe_margin"]    = totSafeMargin;

    auto oil = doc["oil"].to<JsonObject>();
    oil["startup_pressure"]  = oilStartupPressure;
    oil["startup_pct"]       = oilStartupPct;
    oil["startup_min_bar"]   = oilStartupMinBar;
    oil["running_min"]       = oilRunningMin;
    oil["map_min"]           = oilMapMin;
    oil["map_max"]           = oilMapMax;
    oil["use_throttle_map"]  = oilUseThrottleMap;
    oil["adjust_scale"]      = oilAdjustScale;
    oil["min_pct"]           = oilMinPct;
    oil["failsafe_delay_ms"] = oilFailsafeDelayMs;
    oil["failsafe_pct"]      = oilFailsafePct;

    auto su = doc["sequence"]["startup"].to<JsonObject>();
    su["oil_arm_timeout_ms"]      = startupOilArmTimeoutMs;
    su["pre_ign_rpm"]             = preIgnRpm;
    su["pre_ign_spark_ms"]        = preIgnSparkMs;
    su["flame_timeout_ms"]        = flameTimeoutMs;
    su["flame_check_interval_ms"] = flameCheckIntervalMs;
    su["flame_required_count"]    = flameRequiredCount;
    su["rpm_target"]              = spoolRpmTarget;
    su["rpm_timeout_ms"]          = spoolTimeoutMs;
    su["safety_hold_ms"]          = safetyHoldMs;
    su["final_check_rpm"]         = safetyHoldFinalRpm;
    su["starter_demand"]           = starterDemand;
    su["starter_timeout_ms"]       = starterTimeoutMs;
    su["temp_confirm_target"]      = tempConfirmTarget;
    su["temp_confirm_timeout"]     = tempConfirmTimeoutMs;
    su["wait_for_input_ch"]        = waitForInputChannel;
    su["wait_for_input_state"]     = waitForInputExpected;
    su["wait_for_input_timeout"]   = waitForInputTimeoutMs;
    su["timed_delay_ms"]           = timedDelayMs;
    su["modified_idle_multiplier"] = modifiedIdleMultiplier;
    su["fuel_pulse_ms"]            = fuelPulsePulseMs;
    su["fuel_off_ms"]              = fuelPulseOffMs;
    su["wait_tot_target"]          = waitTotCoolTarget;
    su["wait_tot_timeout"]         = waitTotCoolTimeoutMs;
    su["throttle_set_pct"]         = throttleSetPct;
    su["preheat_ms"]               = preHeatMs;
    su["oil_pump_on_pct"]          = oilPumpOnPct;
    su["flame_turn_off_igniter"]   = flameConfirmTurnOffIgniter;
    su["safety_turn_off_starter"]  = safetyHoldTurnOffStarter;
    su["safety_turn_off_starter_en"] = safetyHoldTurnOffStarterEn;
    su["safety_turn_off_igniter"]       = safetyHoldTurnOffIgniter;
    su["spool_cut_starter_on_exit"]     = spoolCutStarterOnExit;
    su["spool_cut_starter_en_on_exit"]  = spoolCutStarterEnOnExit;
    su["hot_start_tot_threshold"]       = hotStartTotThreshold;
    su["oil_prime_use_scavenge"] = oilPrimeUseScavengePump;
    su["fp2_start_pct"]          = fp2StartPct;
    su["fp2_end_pct"]            = fp2EndPct;
    su["fp2_ramp_ms"]            = fp2RampMs;
    su["fp2_demand_pct"]         = fp2DemandPct;
    su["gov_hold_timeout_ms"]    = govHoldTimeoutMs;

    auto sd = doc["sequence"]["shutdown"].to<JsonObject>();
    sd["rpm_drop_threshold"]    = shutdownRpmDropThreshold;
    sd["rpm_drop_timeout_ms"]   = shutdownRpmDropTimeoutMs;
    sd["cooldown_timeout_ms"]   = shutdownCooldownTimeoutMs;
    sd["final_stop_timeout_ms"] = shutdownFinalStopTimeoutMs;
    sd["oil_scavenge_ms"]       = finalStopOilScavengeMs;
    sd["cooldown_use_scavenge"]  = cooldownUseScavengePump;
    sd["cooldown_use_starter"]  = cooldownUseStarter;
    sd["cooldown_use_oil"]      = cooldownUseOilPump;
    sd["cooldown_starter_pct"]        = cooldownStarterPct;
    sd["cooldown_oil_pct"]            = cooldownOilPct;
    sd["cooldown_oil_pressure_bar"]   = cooldownOilPressureTarget;
    sd["rpm_zero_threshold"]          = rpmZeroThreshold;

    auto th = doc["throttle"].to<JsonObject>();
    th["ramp_up_ms"]   = throttleRampUpMs;
    th["ramp_down_ms"] = throttleRampDownMs;
    th["idle_min_pct"] = throttleIdleMinPct;
    th["idle_max_pct"] = throttleIdleMaxPct;
    th["expo"]         = throttleExpo;

    auto di = doc["dynamic_idle"].to<JsonObject>();
    di["target_rpm"]    = idleTargetRpm;
    di["ramp_up_ms"]    = idleRampUpMs;
    di["ramp_down_ms"]  = idleRampDownMs;
    di["deadband_rpm"]  = idleDeadbandRpm;
    di["rpm_limit"]     = idleRpmLimit;
    di["min_multiplier"]= idleMinMultiplier;
    di["use_n2"]        = idleUseN2;
    di["i_gain"]        = idleIGain;
    di["i_max"]         = idleIMax;

    auto sf = doc["safety"].to<JsonObject>();
    sf["check_interval_ms"]           = safetyCheckIntervalMs;
    sf["flameout_shutdown_ms"]        = flameoutShutdownMs;
    sf["egt_source"]                  = egtSource;
    sf["flameout_source"]             = flameoutSource;
    sf["flameout_n1_min_rpm"]         = flameoutN1MinRpm;
    sf["flameout_tot_drop_c"]         = flameoutTotDropC;
    sf["tot_rise_rate_limit_deg_s"]   = totRiseRateLimitDegPerSec;
    sf["tit_limit_c"]                 = titLimit;
    sf["oil_temp_limit_c"]            = oilTempLimit;
    sf["fuel_press_min_bar"]          = fuelPressMin;
    sf["batt_volt_min_v"]             = battVoltMin;
    sf["surge_detect_rpm_variance"]   = surgeDetectRpmVariance;

    auto gov = doc["governor"].to<JsonObject>();
    gov["target_rpm"]    = governorTargetRpm;
    gov["band_rpm"]      = governorBandRpm;
    gov["kp"]            = governorKp;
    gov["pitch_kp"]        = governorPitchKp;
    gov["pitch_ramp_sec"]  = governorPitchRampSec;
    gov["pitch_idle_deg"]= propPitchIdleDeg;
    gov["pitch_max_deg"] = propPitchMaxDeg;

    auto glw = doc["glow_plug"].to<JsonObject>();
    glw["preheat_ms"]     = glowPreheatMs;
    glw["preheat_max_pct"]= glowPreheatMaxPct;
    glw["hold_pct"]       = glowHoldPct;
    glw["wait_until_hot"] = glowWaitUntilHot;

    auto cal = doc["calibration"].to<JsonObject>();
    cal["throttle_min_raw"] = throttleMinRaw;
    cal["throttle_max_raw"] = throttleMaxRaw;
    cal["idle_min_raw"]     = idleMinRaw;
    cal["idle_max_raw"]     = idleMaxRaw;
    cal["flame_threshold"]  = flameThreshold;
    auto poly = cal["oil_poly"].to<JsonObject>();
    poly["a"]     = oilPolyA;
    poly["b"]     = oilPolyB;
    poly["c"]     = oilPolyC;
    poly["d"]     = oilPolyD;
    poly["x_min"] = oilPolyXMin;
    poly["x_max"]        = oilPolyXMax;
    cal["p1_raw_min"]         = p1RawMin;
    cal["p1_raw_max"]         = p1RawMax;
    cal["p1_val_max"]         = p1ValMax;
    cal["p2_raw_min"]         = p2RawMin;
    cal["p2_raw_max"]         = p2RawMax;
    cal["p2_val_max"]         = p2ValMax;
    cal["p1_zero_bar"]        = p1ZeroBar;
    cal["p2_zero_bar"]        = p2ZeroBar;
    cal["fuel_press_raw_min"] = fuelPressRawMin;
    cal["fuel_press_raw_max"] = fuelPressRawMax;
    cal["fuel_press_val_max"] = fuelPressValMax;
    cal["fuel_flow_raw_min"]  = fuelFlowRawMin;
    cal["fuel_flow_raw_max"]  = fuelFlowRawMax;
    cal["fuel_flow_val_max"]  = fuelFlowValMax;

    auto rl = doc["relight"].to<JsonObject>();
    rl["enabled"]            = relightEnabled;
    rl["confirm_source"]     = relightConfirmSource;
    rl["min_rpm"]            = relightMinRpm;
    rl["confirm_rpm"]        = relightConfirmRpm;
    rl["tot_rise_c"]         = relightTotRiseC;
    rl["relight_timeout_ms"] = relightTimeoutMs;

    auto tl = doc["tools"].to<JsonObject>();
    tl["fuel_prime_ms"]    = toolFuelPrimeMs;
    tl["oil_prime_ms"]     = toolOilPrimeMs;
    tl["ign_test_ms"]      = toolIgnTestMs;
    tl["start_test_ms"]    = toolStartTestMs;
    tl["fuel_sol_test_ms"] = toolFuelSolTestMs;

    auto tm = doc["telemetry"].to<JsonObject>();
    tm["ws_interval_ms"]       = wsIntervalMs;
    tm["snapshot_interval_ms"] = snapshotIntervalMs;
    tm["log_standby"]          = logStandby;

    auto sa = doc["starter_assist"].to<JsonObject>();
    sa["pct"]            = starterAssistPct;
    sa["exit_rpm"]       = starterAssistExitRpm;
    sa["ramp_pct_per_s"] = starterRampPctPerSec;

    auto oilx = doc["oil_advanced"].to<JsonObject>();
    oilx["zero_bar"]     = oilZeroBar;
    oilx["deadband_bar"] = oilPressureDeadband;


    auto sob = doc["standby_oil"].to<JsonObject>();
    sob["source"]    = standbyOilSource;
    sob["rpm_limit"] = standbyOilRpmLimit;
    sob["feed_pct"]  = standbyOilFeedPct;

    auto limp = doc["limp_mode"].to<JsonObject>();
    limp["max_throttle_pct"] = limpMaxThrottlePct;

    auto misc = doc["misc"].to<JsonObject>();
    misc["cooldown_skip_hold_ms"] = cooldownSkipHoldMs;
    misc["igniter_on_start"]      = igniterOnStart;

    auto fp = doc["fuel_pump"].to<JsonObject>();
    fp["idle_min_pct"] = fuelPumpIdleMinPct;
    fp["idle_max_pct"] = fuelPumpIdleMaxPct;

    auto rh = doc["rpm_health"].to<JsonObject>();
    rh["jump_threshold"]   = rpmJumpThreshold;
    rh["zero_stuck_ticks"] = rpmZeroStuckTicks;

    auto cl = doc["cluster"].to<JsonObject>();
    cl["n1_warn_rpm"]  = n1WarnRpm;
    cl["n2_warn_rpm"]  = n2WarnRpm;
    cl["tot_warn_c"]   = totWarnC;
    cl["oil_warn_bar"] = oilWarnBar;
    cl["enabled"]      = clusterEnabled;

    auto disp = doc["display"].to<JsonObject>();
    disp["pressure_sensors"] = pressureSensorsEnabled;

    auto rc = doc["rc_input"].to<JsonObject>();
    rc["min_us"]      = rcMinUs;
    rc["max_us"]      = rcMaxUs;
    rc["failsafe_ms"] = rcFailsafeMs;

    auto ab = doc["afterburner"].to<JsonObject>();
    ab["min_n1"]              = abMinN1;
    ab["max_n1"]              = abMaxN1;
    ab["max_tot_for_light"]   = abMaxTotForLight;
    ab["throttle_threshold"]  = abThrottleThreshold;
    ab["use_torch"]            = abUseTorch;
    ab["use_igniter"]          = abUseIgniter;
    ab["torch_spike_pct"]     = abTorchSpikePct;
    ab["torch_duration_ms"]   = abTorchDurationMs;
    ab["torch_tot_limit"]     = abTorchTotLimit;
    ab["flame_mode"]           = abFlameMode;
    ab["tot_rise_deg_c"]      = abTotRiseDegC;
    ab["tot_rise_window_ms"]  = abTotRiseWindowMs;
    ab["assume_ignited_ms"]   = abAssumeIgnitedMs;
    ab["flame_timeout_ms"]    = abFlameTimeoutMs;
    ab["pump_min_pct"]         = abPumpMinPct;
    ab["pump_max_pct"]         = abPumpMaxPct;
    ab["pump_control_mode"]    = abPumpControlMode;
    ab["pump_follow_throttle"] = (abPumpControlMode == 1);
    ab["main_fuel_offset_pct"] = abMainFuelOffsetPct;
    ab["stabilize_ms"]         = abStabilizeMs;
    ab["stabilize_max_tot"]    = abStabilizeMaxTot;

    auto sl = doc["session_log"].to<JsonObject>();
    sl["n1"]       = (bool)(sessionLogMask & SLOG_N1);
    sl["n2"]       = (bool)(sessionLogMask & SLOG_N2);
    sl["tot"]      = (bool)(sessionLogMask & SLOG_TOT);
    sl["oil"]      = (bool)(sessionLogMask & SLOG_OIL);
    sl["p1"]       = (bool)(sessionLogMask & SLOG_P1);
    sl["p2"]       = (bool)(sessionLogMask & SLOG_P2);
    sl["throttle"] = (bool)(sessionLogMask & SLOG_THR);
    sl["mode"]       = (bool)(sessionLogMask & SLOG_MODE);
    sl["tit"]        = (bool)(sessionLogMask & SLOG_TIT);
    sl["batt"]       = (bool)(sessionLogMask & SLOG_BATT);
    sl["fuel_press"] = (bool)(sessionLogMask & SLOG_FUEL_PRESS);
    sl["fuel_flow"]  = (bool)(sessionLogMask & SLOG_FUEL_FLOW);
    sl["glow"]       = (bool)(sessionLogMask & SLOG_GLOW);
    sl["fp2"]        = (bool)(sessionLogMask & SLOG_FP2);
    sl["ab"]         = (bool)(sessionLogMask & SLOG_AB);
    sl["prop"]       = (bool)(sessionLogMask & SLOG_PROP);
    sl["oil_pct"]    = (bool)(sessionLogMask & SLOG_OIL_PCT);
    sl["interval_ms"]= sessionLogIntervalMs;

    auto stats = doc["stats"].to<JsonObject>();
    stats["total_run_seconds"] = totalRunSeconds;

    // ── Automation rules ──────────────────────────────────────────
    if (ruleCount > 0) {
        auto arr = doc["rules"].to<JsonArray>();
        for (int i = 0; i < ruleCount; i++) {
            const Rule& r = rules[i];
            auto jr = arr.add<JsonObject>();
            jr["enabled"]   = r.enabled;
            jr["sensor"]    = r.sensor;
            jr["op"]        = r.op;
            jr["threshold"] = r.threshold;
            jr["actuator"]  = r.actuator;
            jr["on_value"]  = r.onValue;
            jr["off_value"] = r.offValue;
            jr["name"]      = r.name;
        }
    }
}
