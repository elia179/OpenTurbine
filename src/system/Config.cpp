#include "Config.h"
#include "HardwareConfig.h"
#include "hardware_profile.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <Arduino.h>
#include <math.h>
#include "RulesEngine.h"     // after Arduino.h — needs constrain()
#include "FlightRecorder.h"

namespace {
int8_t ruleSourceHandle(const char* id) {
    if (!id || !id[0]) return -1;
    if (!strcmp(id, "oil_temp") || !strcmp(id, "oil_temp_main")) return RulesEngine::OIL_TEMP;
    if (!strcmp(id, "tot") || !strcmp(id, "tot_main")) return RulesEngine::TOT;
    if (!strcmp(id, "n1_main") || !strcmp(id, "n1_rpm")) return RulesEngine::N1_RPM;
    if (!strcmp(id, "n2_main") || !strcmp(id, "n2_rpm")) return RulesEngine::N2_RPM;
    if (!strcmp(id, "oil_pressure_main") || !strcmp(id, "oil_press")) return RulesEngine::OIL_PRESS;
    if (!strcmp(id, "primary_n1")) return RulesEngine::N1_RPM;
    if (!strcmp(id, "primary_n2")) return RulesEngine::N2_RPM;
    if (!strcmp(id, "primary_egt")) return RulesEngine::TOT;
    if (!strcmp(id, "operator_throttle") || !strcmp(id, "throttle_input") || !strcmp(id, "throttle_in") || !strcmp(id, "throttle_input_main")) return RulesEngine::THROTTLE_INPUT;
    if (!strcmp(id, "tit") || !strcmp(id, "tit_main")) return RulesEngine::TIT;
    if (!strcmp(id, "batt_voltage") || !strcmp(id, "batt_voltage_main") || !strcmp(id, "battery_voltage")) return RulesEngine::BATT_V;
    if (!strcmp(id, "fuel_press") || !strcmp(id, "fuel_pressure_main")) return RulesEngine::FUEL_PRESS;
    if (!strcmp(id, "fuel_flow") || !strcmp(id, "fuel_flow_main")) return RulesEngine::FUEL_FLOW;
    if (!strcmp(id, "p1") || !strcmp(id, "p1_main")) return RulesEngine::P1;
    if (!strcmp(id, "p2") || !strcmp(id, "p2_main")) return RulesEngine::P2;
    if (!strcmp(id, "torque") || !strcmp(id, "torque_main")) return RulesEngine::TORQUE;
    if (!strcmp(id, "flame") || !strcmp(id, "flame_main")) return RulesEngine::FLAME;
    if (!strcmp(id, "idle_input") || !strcmp(id, "idle_in") || !strcmp(id, "idle_input_main") || !strcmp(id, "operator_idle")) return RulesEngine::IDLE_INPUT;
    if (!strcmp(id, "ab_flame") || !strcmp(id, "ab_flame_main")) return RulesEngine::AB_FLAME;
    if (!strcmp(id, "glow_current") || !strcmp(id, "glow_current_main")) return RulesEngine::GLOW_CURRENT;
    if (!strcmp(id, "igniter_current") || !strcmp(id, "igniter_current_main")) return RulesEngine::IGNITER_CURRENT;
    if (!strcmp(id, "igniter2_current") || !strcmp(id, "igniter2_current_main")) return RulesEngine::IGNITER2_CURRENT;
    if (!strcmp(id, "oil_pump_current") || !strcmp(id, "oil_pump_current_main")) return RulesEngine::OIL_PUMP_CURRENT;
    if (!strcmp(id, "ab_input") || !strcmp(id, "ab_input_main")) return RulesEngine::AB_INPUT;
    if (!strcmp(id, "start_switch")) return RulesEngine::START_SWITCH;
    if (!strcmp(id, "stop_switch")) return RulesEngine::STOP_SWITCH;
    if (!strcmp(id, "di0")) return RulesEngine::DI_CH0;
    if (!strcmp(id, "di1")) return RulesEngine::DI_CH1;
    if (!strcmp(id, "di2")) return RulesEngine::DI_CH2;
    if (!strcmp(id, "di3")) return RulesEngine::DI_CH3;
    for (uint8_t i = 0; i < HardwareConfig::channelRegistry.inputCount; ++i) {
        const auto& in = HardwareConfig::channelRegistry.inputs[i];
        if (strcmp(in.id, id) != 0) continue;
        return (int8_t)(ChannelRegistry::INPUT_SENSOR_BASE + i);
    }
    return -1;
}
int8_t ruleTargetHandle(const char* id) {
    if (!id || !id[0]) return -1;
    if (!strcmp(id, "main_fuel") || !strcmp(id, "main_fuel_output") || !strcmp(id, "throttle")) return RulesEngine::THROTTLE;
    if (!strcmp(id, "starter_main") || !strcmp(id, "main_starter") || !strcmp(id, "starter")) return RulesEngine::STARTER;
    if (!strcmp(id, "oil_pump_main") || !strcmp(id, "oil_pump")) return RulesEngine::OIL_PUMP;
    if (!strcmp(id, "cooling_fan_main") || !strcmp(id, "cooling_fan") || !strcmp(id, "cool_fan")) return RulesEngine::COOL_FAN;
    if (!strcmp(id, "bleed_valve_main") || !strcmp(id, "bleed_valve")) return RulesEngine::BLEED_VALVE;
    if (!strcmp(id, "oil_scavenge_main") || !strcmp(id, "scavenge_pump") || !strcmp(id, "oil_scavenge_pump")) return RulesEngine::OIL_SCAVENGE;
    if (!strcmp(id, "fuel_pump") || !strcmp(id, "fuel_pump2") || !strcmp(id, "fuel_pump2_main")) return RulesEngine::FUEL_PUMP2;
    if (!strcmp(id, "main_fuel_shutoff") || !strcmp(id, "fuel_shutoff") || !strcmp(id, "fuel_sol")) return RulesEngine::FUEL_SOL;
    if (!strcmp(id, "igniter") || !strcmp(id, "igniter_main")) return RulesEngine::IGNITER;
    if (!strcmp(id, "ab_igniter") || !strcmp(id, "igniter2_main") || !strcmp(id, "igniter2")) return RulesEngine::IGNITER2;
    if (!strcmp(id, "ab_solenoid") || !strcmp(id, "ab_sol") || !strcmp(id, "ab_solenoid_main")) return RulesEngine::AB_SOL;
    if (!strcmp(id, "ab_pump") || !strcmp(id, "ab_pump_main")) return RulesEngine::AB_PUMP;
    if (!strcmp(id, "air_starter") || !strcmp(id, "airstarter_sol") || !strcmp(id, "airstarter_main")) return RulesEngine::AIRSTARTER;
    if (!strcmp(id, "glow_plug") || !strcmp(id, "glow_plug_main")) return RulesEngine::GLOW_PLUG;
    if (!strcmp(id, "prop_pitch") || !strcmp(id, "prop_pitch_main")) return RulesEngine::PROP_PITCH;
    if (!strcmp(id, "request_shutdown")) return RulesEngine::REQUEST_SHUTDOWN;
    if (!strcmp(id, "request_fault")) return RulesEngine::REQUEST_FAULT;
    for (uint8_t i = 0; i < HardwareConfig::channelRegistry.outputCount; ++i) {
        const auto& out = HardwareConfig::channelRegistry.outputs[i];
        if (strcmp(out.id, id) != 0) continue;
        if (!HardwareConfig::channelRegistry.ownsCoreOutput(out) &&
            !HardwareConfig::channelRegistry.boundToCoreOutput(out))
            return (int8_t)(ChannelRegistry::OUTPUT_ACTUATOR_BASE + i);
        return -1;
    }
    return -1;
}
}

// hardware_profile.h controller option → Config default (file wins once saved)
#ifdef OT_DYNAMIC_IDLE_USE_N2
static constexpr bool kIdleUseN2Default = true;
#else
static constexpr bool kIdleUseN2Default = false;
#endif

// ── Static member definitions ─────────────────────────────────
float Config::rpmLimit              = 100000;
float Config::n2RpmLimit            = 0;
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
float Config::throttleIdleMaxPct    = 18;
float Config::fuelPumpMinPct        = 0;   // 0 = not calibrated; measured via the fuel-pump min-spin calibration
float Config::throttleExpo          = 0.0f;  // 0 = linear by default
bool  Config::pullbackN1Enabled     = true;
bool  Config::pullbackN2Enabled     = false;
bool  Config::pullbackEgtEnabled    = true;
float Config::pullbackN1SoftRpm     = 95000.0f;
float Config::pullbackN1HardRpm     = 100000.0f;
float Config::pullbackN2SoftRpm     = 0.0f;
float Config::pullbackN2HardRpm     = 0.0f;
float Config::pullbackEgtSoftC      = 700.0f;
float Config::pullbackEgtHardC      = 750.0f;
float Config::pullbackMinThrottlePct = 8.0f;
float Config::pullbackStrength      = 1.0f;
int   Config::rpmLimiterMode            = 0;
float Config::pullbackLookaheadMs       = 1500.0f;
float Config::pullbackNearLimitRampUpMs = 4000.0f;
float Config::pullbackApproachZoneRpm   = 0.0f;
float Config::rpmAccelFilter            = 0.20f;

float Config::idleTargetRpm         = 44000;
float Config::idleRampUpMs          = 10000;
float Config::idleRampDownMs        = 20000;
float Config::idleDeadbandRpm       = 300;
float Config::idleRpmLimit          = 60000;
float Config::idleMinMultiplier     = 0.75f;
float Config::idleMaxMultiplier     = 1.50f;
bool  Config::idleUseN2             = kIdleUseN2Default;
float Config::idleIGain             = 0.0f;   // 0 = off by default (pure ramp mode), enable in config
float Config::idleIMax              = 0.10f;  // ±10% integral authority
int   Config::idleMode                  = 0;
float Config::idleDecelEnterRpm         = 1000.0f;
float Config::idleDecelDropPct          = 2.0f;
float Config::idleLookaheadMs           = 2500.0f;
float Config::idleSettleBandRpm         = 1500.0f;
float Config::idleFullResponseRpm       = 12000.0f;
float Config::idleTrimUpPctPerSec       = 4.0f;
float Config::idleTrimDownPctPerSec     = 2.0f;
float Config::idleLearnRate             = 0.02f;
float Config::idleLearnAccelMax         = 1200.0f;

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
int      Config::relightIgnitionTarget = 0;
int      Config::relightConfirmSource = 0;
float    Config::relightMinRpm       = 30000.0f;
float    Config::relightConfirmRpm   = 0.0f;
float    Config::relightTotRiseC     = 30.0f;
int      Config::relightTimeoutMs    = 10000;   // 10 s continuous ignition window before faulting

uint32_t Config::toolFuelPrimeMs    = 3000;
uint32_t Config::toolOilPrimeMs     = 5000;
uint32_t Config::toolIgnTestMs      = 2000;
uint32_t Config::toolIgn2TestMs     = 2000;
uint32_t Config::toolGlowTestMs     = 10000;
float    Config::toolGlowTestPct    = 100.0f;
uint32_t Config::toolStartTestMs    = 2000;
float    Config::toolStartTestPct   = 30.0f;
uint32_t Config::toolFuelSolTestMs  = 1000;
uint32_t Config::toolIdleTestMs     = 3000;
uint32_t Config::toolOilScavTestMs  = 2000;
uint32_t Config::toolCoolFanTestMs  = 3000;
uint32_t Config::toolAirstarterTestMs = 1000;
uint32_t Config::toolBleedValveTestMs = 1000;
uint32_t Config::toolFuelPump2TestMs = 3000;
float    Config::toolFuelPump2TestPct = 30.0f;
uint32_t Config::toolAbSolTestMs    = 1000;
uint32_t Config::toolAbPumpTestMs   = 2000;
float    Config::toolAbPumpTestPct  = 30.0f;
uint32_t Config::toolStarterEnTestMs = 1000;
uint32_t Config::toolPropPitchTestMs = 3000;
float    Config::toolPropPitchTestPct = 50.0f;

uint32_t Config::wsIntervalMs       = 333;
uint32_t Config::snapshotIntervalMs = 10000;
uint32_t Config::controlLoopHz      = 400;
bool     Config::logStandby         = false;

float    Config::starterLowRpmSupportPct = 15.0f;
float    Config::starterLowRpmSupportDisengageRpm = 1000.0f;

float    Config::starterStartupRampPctPerSec = 10.0f;
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
float    Config::standbyOilFeedBar   = 0.0f;

float    Config::limpMaxThrottlePct  = 50.0f;
bool     Config::igniterOnStart      = true;
int      Config::manualRelightIgnitionTarget = 0;

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
bool     Config::abUseTorch                 = false;
bool     Config::abUseIgniter               = false;
float    Config::abTorchSpikePct            = 30.0f;
int      Config::abTorchDurationMs          = 400;
float    Config::abTorchTotLimit            = 0.0f;      // 0 = disabled
int      Config::abFlameMode                = 2;         // 2=timed (safest default)
float    Config::abTotRiseDegC              = 30.0f;
int      Config::abTotRiseWindowMs          = 2000;
int      Config::abAssumeIgnitedMs          = 1500;
int      Config::abFlameTimeoutMs           = 3000;
float    Config::abLightupPumpPct           = 80.0f;
float    Config::abPumpMinPct               = 80.0f;
float    Config::abPumpMaxPct               = 100.0f;
int      Config::abPumpControlMode          = 0;
float    Config::abMainFuelOffsetPct        = 0.0f;
int      Config::abStabilizeMs              = 1000;
float    Config::abStabilizeMaxTot          = 0.0f;      // 0 = disabled

float    Config::rpmJumpThreshold    = 0.40f;
int      Config::rpmZeroStuckTicks   = 5;

float    Config::n1WarnRpm          = 0.0f;       // 0 = auto (rpmLimit * 0.9)
float    Config::n2WarnRpm          = 22000.0f;
float    Config::totWarnC           = 0.0f;       // 0 = auto (selected EGT limit - totSafeMargin)
float    Config::oilWarnBar         = 0.0f;       // 0 = auto (oilRunningMin)
bool     Config::clusterEnabled     = true;

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

int   Config::glowPreheatMs         = 10000;
float Config::glowPreheatMaxPct     = 80.0f;
float Config::glowHoldPct           = 30.0f;
bool  Config::glowWaitUntilHot      = false;

volatile uint32_t Config::totalRunSeconds    = 0;
volatile uint32_t Config::startAttemptCount  = 0;
volatile uint32_t Config::runCount           = 0;
// Guards the read-modify-write of the three persisted counters above so a
// Core 0 config-restore merge cannot lose a concurrent Core 1 increment.
static portMUX_TYPE s_statsMux = portMUX_INITIALIZER_UNLOCKED;

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
int   Config::fuelPressRawMin       = 0;
int   Config::fuelPressRawMax       = 4095;
float Config::fuelPressValMax       = 10.0f;
int   Config::fuelFlowRawMin        = 0;
int   Config::fuelFlowRawMax        = 4095;
float Config::fuelFlowValMax        = 10.0f;

char  Config::profileId[64]         = {};
char  Config::uiTheme[16]           = "carbon";
bool  Config::profileMatch          = false;
char  Config::loadWarning[192]      = {};
static SemaphoreHandle_t s_configWriteMutex = nullptr;

static void inhibitStartForConfigWriteFailure() {
    EngineData::instance().configStorageFault = true;
    strncpy(EngineData::instance().faultDescription,
        "Cannot start: the ECU configuration could not be written to storage. "
        "Check or re-upload the filesystem before operating the engine.",
        sizeof(EngineData::instance().faultDescription) - 1);
    EngineData::instance().faultDescription[
        sizeof(EngineData::instance().faultDescription) - 1] = '\0';
    Config::profileMatch = false;
}

static void inhibitStartForProfileMismatch() {
    EngineData::instance().configStorageFault = false;
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
    if (ChannelRegistry::isInputSensor(s)) {
        uint8_t idx = ChannelRegistry::inputIndexFromSensor(s);
        return idx < HardwareConfig::channelRegistry.inputCount &&
               HardwareConfig::channelRegistry.inputs[idx].installed &&
               HardwareConfig::channelRegistry.inputs[idx].pin >= 0;
    }
    switch (s) {
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

bool ruleActuatorAvailable(uint8_t a) {
    if (ChannelRegistry::isOutputActuator(a)) {
        uint8_t idx = ChannelRegistry::outputIndexFromActuator(a);
        if (idx >= HardwareConfig::channelRegistry.outputCount) return false;
        const auto& out = HardwareConfig::channelRegistry.outputs[idx];
        return out.installed &&
               out.pin >= 0 &&
               !HardwareConfig::channelRegistry.ownsCoreOutput(out) &&
               !HardwareConfig::channelRegistry.boundToCoreOutput(out);
    }
    switch (a) {
        case 0:  return HardwareConfig::hasCoolFan;
        case 1:  return HardwareConfig::hasBleedValve;
        case 2:  return HardwareConfig::hasFuelPump2;
        case 3:  return HardwareConfig::hasOilScavengePump;
        case 4:  return HardwareConfig::hasThrottle;
        case 5:  return HardwareConfig::hasStarter;
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
    if (!v.is<float>() && !v.is<double>() && !v.is<int>() && !v.is<long>() &&
        !v.is<unsigned int>() && !v.is<unsigned long>()) return false;
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

bool validRuleId(JsonVariantConst v, size_t maxLen,
                 int8_t (*resolve)(const char*),
                 bool (*available)(uint8_t)) {
    if (!present(v)) return false;
    if (!v.is<const char*>()) return false;
    const char* id = v.as<const char*>();
    if (!id) return false;
    if (!id[0]) return false;
    if (strlen(id) >= maxLen) return false;
    int8_t handle = resolve(id);
    return handle >= 0 && available((uint8_t)handle);
}

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
    // Safety-limit upper bounds are deliberately loose: values above the
    // recommended caps (rpm_limit 500000, tot_limit 1400) are accepted and
    // flagged via Config::loadWarning instead of rejected (warn, don't block).
    if (!validNumber(eng["rpm_limit"], 1000.0f, 1000000000.0f) ||
        !validNumber(eng["n2_rpm_limit"], 0.0f, 1000000000.0f) ||
        !validNumber(eng["min_rpm"], 0.0f, 1000000000.0f) ||
        !validNumber(eng["tot_limit"], 0.0f, 100000.0f) ||
        !validNumber(eng["tot_cooldown_target"], 0.0f, 100000.0f) ||
        !validNumber(eng["tot_safe_margin"], 0.0f, 100000.0f)) return false;
    if (present(eng["rpm_limit"]) && present(eng["min_rpm"]) && eng["min_rpm"].as<float>() >= eng["rpm_limit"].as<float>()) return false;
    // tot_cooldown_target >= tot_limit is accepted (warn only) so the
    // config page's "Save anyway" flow works instead of hard-failing.

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
        !validNumber(th["fuel_pump_min_pct"], 0.0f, 100.0f) ||
        !validNumber(th["idle_max_pct"], 0.0f, 100.0f) ||
        !validNumber(th["expo"], 0.0f, 1.0f) ||
        !validBool(th["pullback_n1"]) ||
        !validBool(th["pullback_n2"]) ||
        !validBool(th["pullback_egt"]) ||
        !validNumber(th["pullback_n1_soft_rpm"], 0.0f, 500000.0f) ||
        !validNumber(th["pullback_n1_hard_rpm"], 0.0f, 500000.0f) ||
        !validNumber(th["pullback_n2_soft_rpm"], 0.0f, 500000.0f) ||
        !validNumber(th["pullback_n2_hard_rpm"], 0.0f, 500000.0f) ||
        !validNumber(th["pullback_egt_soft_c"], 0.0f, 1400.0f) ||
        !validNumber(th["pullback_egt_hard_c"], 0.0f, 1400.0f) ||
        !validNumber(th["pullback_min_pct"], 0.0f, 100.0f) ||
        !validNumber(th["pullback_strength"], 0.0f, 5.0f)) return false;
    if (present(th["pullback_n1_soft_rpm"]) && present(th["pullback_n1_hard_rpm"]) &&
        th["pullback_n1_hard_rpm"].as<float>() > 0.0f &&
        th["pullback_n1_hard_rpm"].as<float>() <= th["pullback_n1_soft_rpm"].as<float>()) return false;
    if (present(th["pullback_n2_soft_rpm"]) && present(th["pullback_n2_hard_rpm"]) &&
        th["pullback_n2_hard_rpm"].as<float>() > 0.0f &&
        th["pullback_n2_hard_rpm"].as<float>() <= th["pullback_n2_soft_rpm"].as<float>()) return false;
    if (present(th["pullback_egt_soft_c"]) && present(th["pullback_egt_hard_c"]) &&
        th["pullback_egt_hard_c"].as<float>() > 0.0f &&
        th["pullback_egt_hard_c"].as<float>() <= th["pullback_egt_soft_c"].as<float>()) return false;


    JsonVariantConst tools = doc["tools"];
    if (present(tools) && (!tools.is<JsonObjectConst>() ||
        !validInt(tools["fuel_prime_ms"], 100, 60000) ||
        !validInt(tools["oil_prime_ms"], 100, 60000) ||
        !validInt(tools["ign_test_ms"], 100, 60000) ||
        !validInt(tools["ign2_test_ms"], 100, 60000) ||
        !validInt(tools["glow_test_ms"], 100, 60000) ||
        !validNumber(tools["glow_test_pct"], 0.0f, 100.0f) ||
        !validInt(tools["start_test_ms"], 100, 60000) ||
        !validNumber(tools["start_test_pct"], 0.0f, 100.0f) ||
        !validInt(tools["fuel_sol_test_ms"], 50, 60000) ||
        !validInt(tools["idle_test_ms"], 100, 60000) ||
        !validInt(tools["oil_scav_test_ms"], 100, 60000) ||
        !validInt(tools["cool_fan_test_ms"], 100, 60000) ||
        !validInt(tools["airstarter_test_ms"], 50, 60000) ||
        !validInt(tools["bleed_valve_test_ms"], 50, 60000) ||
        !validInt(tools["fuel_pump2_test_ms"], 100, 60000) ||
        !validNumber(tools["fuel_pump2_test_pct"], 0.0f, 100.0f) ||
        !validInt(tools["ab_sol_test_ms"], 50, 60000) ||
        !validInt(tools["ab_pump_test_ms"], 100, 60000) ||
        !validNumber(tools["ab_pump_test_pct"], 0.0f, 100.0f) ||
        !validInt(tools["starter_en_test_ms"], 50, 60000) ||
        !validInt(tools["prop_pitch_test_ms"], 100, 60000) ||
        !validNumber(tools["prop_pitch_test_pct"], 0.0f, 100.0f))) return false;

    JsonVariantConst so = doc["standby_oil"];
    if (present(so) && (!so.is<JsonObjectConst>() ||
        !validInt(so["source"], 0, 2) ||
        !validNumber(so["rpm_limit"], 0.0f, 500000.0f) ||
        !validNumber(so["feed_pct"], 0.0f, 100.0f) ||
        !validNumber(so["feed_bar"], 0.0f, 20.0f))) return false;

    JsonVariantConst sf = doc["safety"];
    if (!validInt(sf["check_interval_ms"], 10, 60000) ||
        !validNumber(sf["flameout_shutdown_ms"], 100.0f, 60000.0f) ||
        !validInt(sf["egt_source"], 0, 2) ||
        !validInt(sf["flameout_source"], 0, 3) ||
        !validNumber(sf["flameout_n1_min_rpm"], 0.0f, 500000.0f) ||
        !validNumber(sf["flameout_tot_drop_c"], 0.0f, 1400.0f) ||
        !validNumber(sf["tot_rise_rate_limit_deg_s"], 0.0f, 1000.0f) ||
        !validNumber(sf["tit_limit_c"], 0.0f, 100000.0f) ||
        !validNumber(sf["oil_temp_limit_c"], 0.0f, 100000.0f) ||
        !validNumber(sf["fuel_press_min_bar"], 0.0f, 100.0f) ||
        !validNumber(sf["batt_volt_min_v"], 0.0f, 80.0f) ||
        !validNumber(sf["surge_detect_rpm_variance"], 0.0f, 500000.0f)) return false;

    JsonVariantConst rl = doc["relight"];
    if (present(rl) && (!rl.is<JsonObjectConst>() ||
        !validBool(rl["enabled"]) ||
        !validInt(rl["ignition_target"], 0, 2) ||
        !validInt(rl["confirm_source"], 0, 3) ||
        !validNumber(rl["min_rpm"], 0.0f, 500000.0f) ||
        !validNumber(rl["confirm_rpm"], 0.0f, 500000.0f) ||
        !validNumber(rl["tot_rise_c"], 0.0f, 1400.0f) ||
        !validInt(rl["relight_timeout_ms"], 0, 3600000))) return false;

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
    JsonVariantConst poly = cal["oil_poly"];
    if (present(poly) && (!poly.is<JsonObjectConst>() ||
        !validNumber(poly["a"], -1000000.0f, 1000000.0f) ||
        !validNumber(poly["b"], -1000000.0f, 1000000.0f) ||
        !validNumber(poly["c"], -1000000.0f, 1000000.0f) ||
        !validNumber(poly["d"], -1000000.0f, 1000000.0f) ||
        !validNumber(poly["x_min"], 0.0f, 4095.0f) ||
        !validNumber(poly["x_max"], 0.0f, 4095.0f))) return false;

    JsonVariantConst di = doc["dynamic_idle"];
    if (present(di) && (!di.is<JsonObjectConst>() ||
        !validNumber(di["target_rpm"], 0.0f, 500000.0f) ||
        !validNumber(di["ramp_up_ms"], 0.0f, 3600000.0f) ||
        !validNumber(di["ramp_down_ms"], 0.0f, 3600000.0f) ||
        !validNumber(di["deadband_rpm"], 0.0f, 500000.0f) ||
        !validNumber(di["rpm_limit"], 0.0f, 500000.0f) ||
        !validNumber(di["min_multiplier"], 0.0f, 1.0f) ||
        !validNumber(di["max_multiplier"], 1.0f, 3.0f) ||
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
            !validNumber(ab["lightup_pump_pct"], 0.0f, 100.0f) ||
            !validNumber(ab["pump_min_pct"], 0.0f, 100.0f) ||
            !validNumber(ab["pump_max_pct"], 0.0f, 100.0f) ||
            !validInt(ab["pump_control_mode"], 0, 2) ||
            !validNumber(ab["main_fuel_offset_pct"], -20.0f, 50.0f) ||
            !validInt(ab["stabilize_ms"], 0, 3600000) ||
            !validNumber(ab["stabilize_max_tot"], 0.0f, 1400.0f)) return false;
        if (present(ab["pump_min_pct"]) && present(ab["pump_max_pct"]) && ab["pump_max_pct"].as<float>() < ab["pump_min_pct"].as<float>()) return false;
    }

    JsonVariantConst sl = doc["session_log"];
    if (present(sl) && (!sl.is<JsonObjectConst>() || !validInt(sl["interval_ms"], 100, 60000))) return false;

    JsonVariantConst gov = doc["governor"];
    if (present(gov) && (!gov.is<JsonObjectConst>() ||
        !validNumber(gov["target_rpm"], 0.0f, 500000.0f) ||
        !validNumber(gov["band_rpm"], 0.0f, 500000.0f) ||
        !validNumber(gov["kp"], 0.0f, 0.01f) ||
        !validNumber(gov["pitch_kp"], 0.0f, 0.01f) ||
        !validNumber(gov["pitch_ramp_sec"], 0.0f, 3600000.0f))) return false;

    JsonVariantConst glow = doc["glow_plug"];
    if (present(glow) && (!glow.is<JsonObjectConst>() ||
        !validInt(glow["preheat_ms"], 0, 3600000) ||
        !validNumber(glow["preheat_max_pct"], 0.0f, 100.0f) ||
        !validNumber(glow["hold_pct"], 0.0f, 100.0f) ||
        !validBool(glow["wait_until_hot"]))) return false;

    JsonVariantConst rc = doc["rc_input"];
    if (present(rc) && (!rc.is<JsonObjectConst>() ||
        !validInt(rc["failsafe_ms"], 20, 60000))) return false;

    JsonVariantConst misc = doc["misc"];
    if (present(misc) && (!misc.is<JsonObjectConst>() ||
        !validBool(misc["igniter_on_start"]) ||
        !validInt(misc["igniter_on_start_target"], 0, 2) ||
        !validInt(misc["cooldown_skip_hold_ms"], 0, 60000))) return false;

    // Groups below were read/written but never validated — a restore or raw
    // API write could persist values far outside the UI ranges and only get
    // silently clamped (or not) after load.
    // NOTE: these telemetry bounds MUST mirror the boot-load clamps in
    // _fromDoc (ws>=333, snapshot>=500, control_loop 50..1000) so a value the
    // API accepts is exactly the value that survives the next reboot.
    JsonVariantConst tmv = doc["telemetry"];
    if (present(tmv) && (!tmv.is<JsonObjectConst>() ||
        !validInt(tmv["ws_interval_ms"], 333, 60000) ||
        !validInt(tmv["snapshot_interval_ms"], 500, 3600000) ||
        !validInt(tmv["control_loop_hz"], 50, 1000) ||
        !validBool(tmv["log_standby"]))) return false;

    JsonVariantConst sav = doc["starter_control"];
    if (present(sav) && (!sav.is<JsonObjectConst>() ||
        !validNumber(sav["low_rpm_support_pct"], 0.0f, 100.0f) ||
        !validNumber(sav["low_rpm_support_disengage_rpm"], 0.0f, 1000000.0f) ||
        !validNumber(sav["startup_ramp_pct_per_s"], 0.0f, 1000.0f))) return false;

    JsonVariantConst oilxv = doc["oil_advanced"];
    if (present(oilxv) && (!oilxv.is<JsonObjectConst>() ||
        !validNumber(oilxv["zero_bar"], 0.0f, 100.0f) ||
        !validNumber(oilxv["deadband_bar"], 0.0f, 100.0f))) return false;
    // (standby_oil is validated once above near the top of this function.)

    JsonVariantConst limpv = doc["limp_mode"];
    if (present(limpv) && (!limpv.is<JsonObjectConst>() ||
        !validNumber(limpv["max_throttle_pct"], 0.0f, 100.0f))) return false;

    JsonVariantConst clv = doc["cluster"];
    if (present(clv) && (!clv.is<JsonObjectConst>() ||
        !validBool(clv["enabled"]) ||
        !validNumber(clv["n1_warn_rpm"], 0.0f, 1000000.0f) ||
        !validNumber(clv["n2_warn_rpm"], 0.0f, 1000000.0f) ||
        !validNumber(clv["tot_warn_c"], 0.0f, 100000.0f) ||
        !validNumber(clv["oil_warn_bar"], 0.0f, 1000.0f))) return false;

    JsonVariantConst rhs = doc["rpm_health"];
    if (present(rhs) && (!rhs.is<JsonObjectConst>() ||
        !validNumber(rhs["jump_threshold"], 0.001f, 1000.0f) ||
        !validInt(rhs["zero_stuck_ticks"], 1, 10000))) return false;

    JsonVariantConst rules = doc["rules"];
    if (present(rules)) {
        if (!rules.is<JsonArrayConst>() || rules.size() > Config::MAX_RULES) return false;
        for (JsonObjectConst rule : rules.as<JsonArrayConst>()) {
            if (!validBool(rule["enabled"]) ||
                !validInt(rule["kind"], 0, 1) ||
                !validInt(rule["op"], 0, 1) ||
                !validNumber(rule["threshold"], -1000000.0f, 1000000.0f) ||
                !validNumber(rule["on_value"], 0.0f, 1.0f) ||
                !validNumber(rule["off_value"], 0.0f, 1.0f) ||
                !validNumber(rule["hysteresis"], 0.0f, 1000000.0f) ||
                !validNumber(rule["input_min"], -1000000.0f, 1000000.0f) ||
                !validNumber(rule["input_max"], -1000000.0f, 1000000.0f) ||
                !validNumber(rule["output_min"], 0.0f, 1.0f) ||
                !validNumber(rule["output_max"], 0.0f, 1.0f) ||
                !validInt(rule["mode_mask"], 1, 15) ||
                !validRuleId(rule["source"], sizeof(Config::Rule::sourceId), ruleSourceHandle, ruleSensorAvailable) ||
                !validRuleId(rule["target"], sizeof(Config::Rule::targetId), ruleTargetHandle, ruleActuatorAvailable)) return false;
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

    if (!LittleFS.exists(PATH)) {
        Serial.println("[Config] No ecu_config.json - generating defaults");
        if (!save()) {
            inhibitStartForConfigWriteFailure();
            return;
        }
        strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
        profileId[sizeof(profileId) - 1] = '\0';
        profileMatch = true;
        return;
    }

    File f = LittleFS.open(PATH, "r");
    if (!f) {
        Serial.println("[Config] Failed to open ecu_config.json");
        EngineData::instance().configStorageFault = true;
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
        EngineData::instance().configStorageFault = true;
        strncpy(EngineData::instance().faultDescription,
            "Cannot start: ecu_config.json is corrupted (JSON parse error).\n"
            "What to do: Use the web UI Tools page to reset config to defaults, "
            "or re-upload the filesystem image.",
            sizeof(EngineData::instance().faultDescription) - 1);
        EngineData::instance().faultDescription[sizeof(EngineData::instance().faultDescription) - 1] = '\0';
        profileMatch = false;
        return;
    }

    JsonDocument workDoc;
    if (fullDoc[SECTION].is<JsonObject>()) {
        workDoc.set(fullDoc[SECTION]);
    } else if (fullDoc["hardware"].is<JsonObject>()) {
        Serial.println("[Config] Settings missing from ecu_config.json - adding defaults");
        strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
        profileId[sizeof(profileId) - 1] = '\0';
        if (!save()) {
            inhibitStartForConfigWriteFailure();
            return;
        }
        profileMatch = true;
        return;
    } else {
        Serial.println("[Config] Settings section missing from ecu_config.json");
        EngineData::instance().configStorageFault = true;
        strncpy(EngineData::instance().faultDescription,
            "Cannot start: ecu_config.json is missing the settings section.\n"
            "What to do: Use the web UI Tools page to reset config to defaults, "
            "or re-upload the filesystem image.",
            sizeof(EngineData::instance().faultDescription) - 1);
        EngineData::instance().faultDescription[sizeof(EngineData::instance().faultDescription) - 1] = '\0';
        profileMatch = false;
        return;
    }

    const char* id = workDoc["profile_id"] | "";
    strncpy(profileId, id, sizeof(profileId) - 1);
    profileId[sizeof(profileId) - 1] = '\0';
    profileMatch = (strcmp(profileId, HardwareConfig::profileId) == 0);
    if (!profileMatch) {
        // Keep web repair available, but do not run with crossed engine sections.
        Serial.printf("[Config] WARNING: settings profile (%s) does not match hardware profile (%s)"
                      " - START inhibited until repaired\n",
                      profileId, HardwareConfig::profileId);
        _applyDefaults();
        strncpy(profileId, HardwareConfig::profileId, sizeof(profileId) - 1);
        profileId[sizeof(profileId) - 1] = '\0';
        inhibitStartForProfileMismatch();
        return;
    }

    uint8_t ver = workDoc["config_version"] | 0;
    if (ver != CONFIG_VERSION) {
        Serial.printf("[Config] Version mismatch (file=%u expected=%u) - new fields use defaults\n",
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
    if (loadWarning[0]) {
        // Boot-only flight-log markers for out-of-cap safety limits (see the
        // accept+warn block in _fromDoc). Emitted here, not in _fromDoc,
        // because boot load runs on Core 1 — the flight recorder's only
        // permitted producer core; web uploads (Core 0) get the persistent
        // telemetry notice without a flight-log record.
        if (rpmLimit > 500000.0f)
            FlightRecorder::logConfigChange("load_warning:rpm_limit", rpmLimit, 500000.0f);
        if (totLimit > 1400.0f)
            FlightRecorder::logConfigChange("load_warning:tot_limit", totLimit, 1400.0f);
        if (titLimit > 1400.0f)
            FlightRecorder::logConfigChange("load_warning:tit_limit_c", titLimit, 1400.0f);
        if (oilTempLimit > 300.0f)
            FlightRecorder::logConfigChange("load_warning:oil_temp_limit_c", oilTempLimit, 300.0f);
    }
    Serial.printf("[Config] Loaded OK - profile: %s\n", profileId);
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

void Config::autoFillNewlyEnabledSafety(bool prevTit, bool prevOilTemp,
                                        bool prevFuelPress, bool prevBatt, bool prevSurge) {
    // For each of the 5 threshold-based safeties: if it just transitioned
    // OFF->ON (user ticked it) and is still active after hardware sanitize (its
    // sensor is present) but its threshold is 0 (= disabled), fill a sane
    // default so a ticked safety can't sit silently off. This runs only on the
    // enable EVENT, so deliberately setting a threshold to 0 later still
    // disables the safety. Each fill is recorded in the event log.
    auto fill = [](bool was, bool now, float& thr, float def, const char* field) {
        if (!was && now && thr <= 0.0f) {
            FlightRecorder::logConfigChange(field, 0.0f, def);
            Serial.printf("[Config] %s enabled with no threshold - auto-set to %.1f\n",
                          field, (double)def);
            thr = def;
        }
    };
    fill(prevTit,       HardwareConfig::safetyOvertemp && HardwareConfig::hasTit, titLimit, 900.0f, "autofill:tit_limit_c");
    fill(prevOilTemp,   HardwareConfig::safetyOilTempHigh,  oilTempLimit,           120.0f,    "autofill:oil_temp_limit_c");
    fill(prevFuelPress, HardwareConfig::safetyFuelPressLow, fuelPressMin,           0.5f,      "autofill:fuel_press_min_bar");
    fill(prevBatt,      HardwareConfig::safetyBattLow,      battVoltMin,            10.5f,     "autofill:batt_volt_min_v");
    fill(prevSurge,     HardwareConfig::safetySurge,        surgeDetectRpmVariance, 500000.0f, "autofill:surge_variance");
}

void Config::sanitizeForHardware() {
    if ((egtSource == 1 && !HardwareConfig::hasTot) ||
        (egtSource == 2 && !HardwareConfig::hasTit)) {
        egtSource = 0;
    }
    bool relightTargetAvailable = false;
    switch (relightIgnitionTarget) {
        case 1: relightTargetAvailable = HardwareConfig::hasIgniter2; break;
        case 2: relightTargetAvailable = HardwareConfig::hasGlowPlug; break;
        default: relightTargetAvailable = HardwareConfig::hasIgniter; break;
    }
    if ((!HardwareConfig::hasN1Rpm || !relightTargetAvailable) && relightEnabled) {
        relightEnabled = false;
    }
    if ((flameoutSource == 1 && !HardwareConfig::hasFlame) ||
        (flameoutSource == 2 && !HardwareConfig::hasN1Rpm) ||
        (flameoutSource == 3 && effectiveEgtSource() == 0)) {
        flameoutSource = 0;
    }
    if ((relightConfirmSource == 1 && !HardwareConfig::hasFlame) ||
        (relightConfirmSource == 2 && !HardwareConfig::hasN1Rpm) ||
        (relightConfirmSource == 3 && effectiveEgtSource() == 0)) {
        relightConfirmSource = 0;
    }
    const bool hasN1 = HardwareConfig::hasN1Rpm;
    const bool hasN2 = HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm;
    if (idleUseN2 && !hasN2) {
        idleUseN2 = false;
    } else if (!hasN1 && hasN2) {
        idleUseN2 = true;
    }
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
    if (!HardwareConfig::hasOilTemp) sessionLogMask &= ~SLOG_OIL_TEMP;
    if (!HardwareConfig::hasOilPress) sessionLogMask &= ~SLOG_OIL;
    if (!HardwareConfig::hasP1) sessionLogMask &= ~SLOG_P1;
    if (!HardwareConfig::hasP2) sessionLogMask &= ~SLOG_P2;
    if (!HardwareConfig::hasThrottle) sessionLogMask &= ~SLOG_THR;
    if (!HardwareConfig::hasTit) sessionLogMask &= ~SLOG_TIT;
    if (!HardwareConfig::hasBattVoltage) sessionLogMask &= ~SLOG_BATT;
    if (!HardwareConfig::hasFuelPress) sessionLogMask &= ~SLOG_FUEL_PRESS;
    if (!HardwareConfig::hasFuelFlow) sessionLogMask &= ~SLOG_FUEL_FLOW;
    if (!HardwareConfig::hasGlowPlug) sessionLogMask &= ~SLOG_GLOW;
    if (!(HardwareConfig::hasGlowPlug && HardwareConfig::glowPlugType == 2 &&
          HardwareConfig::wetGlowFuelPin >= 0)) sessionLogMask &= ~SLOG_WET_GLOW;
    if (!(HardwareConfig::hasGlowPlug && HardwareConfig::hasGlowCurrentSensor)) sessionLogMask &= ~SLOG_GLOW_CURRENT;
    if (!(HardwareConfig::hasIgniter && HardwareConfig::hasIgniterCurrentSensor)) sessionLogMask &= ~SLOG_IGN_CURRENT;
    if (!(HardwareConfig::hasIgniter2 && HardwareConfig::hasIgniter2CurrentSensor)) sessionLogMask &= ~SLOG_IGN2_CURRENT;
    if (!(HardwareConfig::hasOilPump && HardwareConfig::hasOilPumpCurrentSensor)) sessionLogMask &= ~SLOG_OIL_CURRENT;
    if (!HardwareConfig::hasFuelPump2) sessionLogMask &= ~SLOG_FP2;
    if (!HardwareConfig::hasAfterburner) sessionLogMask &= ~SLOG_AB;
    if (!HardwareConfig::hasPropPitch) sessionLogMask &= ~SLOG_PROP;
    if (!HardwareConfig::hasOilPump) sessionLogMask &= ~SLOG_OIL_PCT;

    int out = 0;
    uint8_t claimedTargets[MAX_RULES] = {};
    int claimedTargetCount = 0;
    for (int i = 0; i < ruleCount; i++) {
        Rule r = rules[i];
        if (!ruleSensorAvailable(r.sensor) || !ruleActuatorAvailable(r.actuator)) continue;
        bool duplicateTarget = false;
        if (r.actuator != 13 && r.actuator != 14) {
            for (int j = 0; j < claimedTargetCount; ++j)
                if (claimedTargets[j] == r.actuator) { duplicateTarget = true; break; }
        }
        if (duplicateTarget) continue;
        r.kind = constrain(r.kind, 0, 1);
        r.op = constrain(r.op, 0, 1);
        r.onValue = constrain(r.onValue, 0.0f, 1.0f);
        r.offValue = constrain(r.offValue, 0.0f, 1.0f);
        r.outputMin = constrain(r.outputMin, 0.0f, 1.0f);
        r.outputMax = constrain(r.outputMax, 0.0f, 1.0f);
        if (r.kind == 1 && (!isfinite(r.inputMin) || !isfinite(r.inputMax) || r.inputMax <= r.inputMin)) continue;
        if (r.hysteresis < 0.0f) r.hysteresis = 0.0f;
        r.modeMask &= 0x0F;
        if (r.modeMask == 0) continue;
        if (r.actuator != 13 && r.actuator != 14)
            claimedTargets[claimedTargetCount++] = r.actuator;
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
    if (!ok) Serial.println("[Config] WARNING: deferred config save failed");
    return ok;
}

void Config::requestRuntimeStatsSave() {
    _runtimeStatsSavePending = true;
}

static void runtimeStatsKey(char* key, size_t len, const char* prefix) {
    const char* profile = Config::profileId[0] ? Config::profileId : HardwareConfig::profileId;
    uint32_t hash = 2166136261u;
    for (const char* p = profile; p && *p; ++p) {
        hash ^= (uint8_t)*p;
        hash *= 16777619u;
    }
    snprintf(key, len, "%s%08lx", prefix, (unsigned long)hash);
}

void Config::loadRuntimeStats() {
    char runKey[14];
    char startKey[14];
    char rcKey[14];
    runtimeStatsKey(runKey, sizeof(runKey), "run");
    runtimeStatsKey(startKey, sizeof(startKey), "sta");
    runtimeStatsKey(rcKey, sizeof(rcKey), "rct");
    Preferences stats;
    if (!stats.begin("ot", true)) {
        Serial.println("[Config] WARNING: failed to open NVS for accumulated runtime read");
        return;
    }
    uint32_t savedRunSeconds = stats.getUInt(runKey, totalRunSeconds);
    uint32_t savedStarts = stats.getUInt(startKey, startAttemptCount);
    uint32_t savedRuns = stats.getUInt(rcKey, runCount);
    stats.end();
    if (savedRunSeconds > totalRunSeconds) totalRunSeconds = savedRunSeconds;
    if (savedStarts > startAttemptCount) startAttemptCount = savedStarts;
    if (savedRuns > runCount) runCount = savedRuns;
}

bool Config::flushPendingRuntimeStats() {
    if (!_runtimeStatsSavePending) return false;
    _runtimeStatsSavePending = false;
    char runKey[14];
    char startKey[14];
    char rcKey[14];
    runtimeStatsKey(runKey, sizeof(runKey), "run");
    runtimeStatsKey(startKey, sizeof(startKey), "sta");
    runtimeStatsKey(rcKey, sizeof(rcKey), "rct");
    Preferences stats;
    if (!stats.begin("ot", false)) {
        Serial.println("[Config] WARNING: failed to open NVS for accumulated runtime");
        return false;
    }
    size_t writtenRun = stats.putUInt(runKey, totalRunSeconds);
    size_t writtenStarts = stats.putUInt(startKey, startAttemptCount);
    size_t writtenRuns = stats.putUInt(rcKey, runCount);
    stats.end();
    if (writtenRun == 0 || writtenStarts == 0 || writtenRuns == 0) {
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
    char runKey[14];
    char startKey[14];
    char rcKey[14];
    runtimeStatsKey(runKey, sizeof(runKey), "run");
    runtimeStatsKey(startKey, sizeof(startKey), "sta");
    runtimeStatsKey(rcKey, sizeof(rcKey), "rct");
    Preferences stats;
    if (!stats.begin("ot", false)) {
        Serial.println("[Config] WARNING: failed to open NVS to clear accumulated runtime");
        return;
    }
    stats.remove(runKey);
    stats.remove(startKey);
    stats.remove(rcKey);
    stats.end();
    portENTER_CRITICAL(&s_statsMux);
    totalRunSeconds = 0;
    startAttemptCount = 0;
    runCount = 0;
    portEXIT_CRITICAL(&s_statsMux);
}

void Config::addRunSeconds(uint32_t seconds) {
    portENTER_CRITICAL(&s_statsMux);
    totalRunSeconds += seconds;
    portEXIT_CRITICAL(&s_statsMux);
}

void Config::incStartAttemptCount() {
    portENTER_CRITICAL(&s_statsMux);
    startAttemptCount = startAttemptCount + 1u;
    portEXIT_CRITICAL(&s_statsMux);
}

void Config::incRunCount() {
    portENTER_CRITICAL(&s_statsMux);
    runCount = runCount + 1u;
    portEXIT_CRITICAL(&s_statsMux);
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
        LittleFS.remove(TMP_PATH);
        LittleFS.remove(BAK_PATH);
        return false;
    }
    if (hadOriginal) LittleFS.remove(BAK_PATH);
    return true;
}

bool Config::isLocked() {
    auto& ed = EngineData::instance();
    auto m = ed.mode;
    bool active = (m == SysMode::STARTUP || m == SysMode::RUNNING || m == SysMode::SHUTDOWN);
    return active && !ed.devMode;
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
    if (isLocked() || !validateJson(doc)) return false;
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
bool Config::applyJsonRuntimeOnly(const JsonDocument& doc) {
    if (isLocked() || !validateJson(doc)) return false;
    const char* id = doc["profile_id"] | "";
    if (!id[0] || strcmp(id, HardwareConfig::profileId) != 0) return false;
    _fromDoc(doc);
    profileMatch = true;
    EngineData::instance().configVersionMismatch = false;
    return true;
}

float Config::applyFuelPumpMinimum(float demand01) {
    float demand = constrain(demand01, 0.0f, 1.0f);
    float minDemand = constrain(fuelPumpMinPct / 100.0f, 0.0f, 1.0f);
    if (minDemand <= 0.0f) return demand;
    return (demand > 0.0f && demand < minDemand) ? 0.0f : demand;
}

void Config::_applyDefaults() {
    // Re-assign every field to its compile-time default so that load() is
    // idempotent: missing JSON keys restore to the default rather than
    // keeping a stale runtime value from a previous load() call.
    rpmLimit = 100000; n2RpmLimit = 0; minRpm = 30000; totLimit = 750;
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
    throttleIdleMaxPct = 18; throttleExpo = 0.0f;
    fuelPumpMinPct = 0;
    pullbackN1Enabled = true; pullbackN2Enabled = false; pullbackEgtEnabled = true;
    pullbackN1SoftRpm = 95000.0f; pullbackN1HardRpm = 100000.0f;
    pullbackN2SoftRpm = 0.0f; pullbackN2HardRpm = 0.0f;
    pullbackEgtSoftC = 700.0f; pullbackEgtHardC = 750.0f;
    pullbackMinThrottlePct = 8.0f; pullbackStrength = 1.0f;
    rpmLimiterMode = 0; pullbackLookaheadMs = 1500.0f; pullbackNearLimitRampUpMs = 4000.0f;
    pullbackApproachZoneRpm = 0.0f; rpmAccelFilter = 0.20f;
    idleTargetRpm = 44000; idleRampUpMs = 10000; idleRampDownMs = 20000;
    idleDeadbandRpm = 300; idleRpmLimit = 60000; idleMinMultiplier = 0.75f; idleMaxMultiplier = 1.50f;
    idleUseN2 = kIdleUseN2Default; idleIGain = 0.0f; idleIMax = 0.10f;
    idleMode = 0; idleDecelEnterRpm = 1000.0f; idleDecelDropPct = 2.0f; idleLookaheadMs = 2500.0f;
    idleSettleBandRpm = 1500.0f; idleFullResponseRpm = 12000.0f; idleTrimUpPctPerSec = 4.0f;
    idleTrimDownPctPerSec = 2.0f; idleLearnRate = 0.02f; idleLearnAccelMax = 1200.0f;
    safetyCheckIntervalMs = 100; flameoutShutdownMs = 3000;
    egtSource = 0; flameoutSource = 0; flameoutN1MinRpm = 0.0f; flameoutTotDropC = 80.0f;
    totRiseRateLimitDegPerSec = 0.0f; titLimit = 0.0f; oilTempLimit = 120.0f;
    fuelPressMin = 0.0f; battVoltMin = 0.0f; surgeDetectRpmVariance = 0.0f;
    relightEnabled = false; relightIgnitionTarget = 0; relightConfirmSource = 0; relightMinRpm = 30000.0f;
    relightConfirmRpm = 0.0f; relightTotRiseC = 30.0f; relightTimeoutMs = 10000;
    toolFuelPrimeMs = 3000; toolOilPrimeMs = 5000; toolIgnTestMs = 2000; toolIgn2TestMs = 2000;
    toolGlowTestMs = 10000; toolGlowTestPct = 100.0f;
    toolStartTestMs = 2000; toolStartTestPct = 30.0f; toolFuelSolTestMs = 1000;
    toolIdleTestMs = 3000; toolOilScavTestMs = 2000; toolCoolFanTestMs = 3000;
    toolAirstarterTestMs = 1000; toolBleedValveTestMs = 1000;
    toolFuelPump2TestMs = 3000; toolFuelPump2TestPct = 30.0f;
    toolAbSolTestMs = 1000; toolAbPumpTestMs = 2000; toolAbPumpTestPct = 30.0f;
    toolStarterEnTestMs = 1000; toolPropPitchTestMs = 3000; toolPropPitchTestPct = 50.0f;
    wsIntervalMs = 333; snapshotIntervalMs = 10000; controlLoopHz = 400; logStandby = false;
    strcpy(uiTheme, "carbon");
    starterLowRpmSupportPct = 15.0f; starterLowRpmSupportDisengageRpm = 1000.0f; starterStartupRampPctPerSec = 10.0f;
    oilZeroBar = 0.1f; oilPressureDeadband = 0.2f;
    standbyOilSource = 0; standbyOilRpmLimit = 100.0f; standbyOilFeedPct = 25.0f;
    standbyOilFeedBar = 0.0f;
    limpMaxThrottlePct = 50.0f; igniterOnStart = true; manualRelightIgnitionTarget = 0;
    cooldownSkipHoldMs = 1000;
    fp2StartPct = 0.0f; fp2EndPct = 80.0f; fp2RampMs = 3000; fp2DemandPct = 0.0f;
    govHoldTimeoutMs = 10000;
    abMinN1 = 30000.0f; abMaxN1 = 0.0f; abMaxTotForLight = 0.0f;
    abThrottleThreshold = 0.80f; abUseTorch = false; abUseIgniter = false;
    abTorchSpikePct = 30.0f; abTorchDurationMs = 400; abTorchTotLimit = 0.0f;
    abFlameMode = 2; abTotRiseDegC = 30.0f; abTotRiseWindowMs = 2000;
    abAssumeIgnitedMs = 1500; abFlameTimeoutMs = 3000;
    abLightupPumpPct = 80.0f; abPumpMinPct = 80.0f; abPumpMaxPct = 100.0f; abPumpControlMode = 0;
    abMainFuelOffsetPct = 0.0f; abStabilizeMs = 1000; abStabilizeMaxTot = 0.0f;
    rpmJumpThreshold = 0.40f; rpmZeroStuckTicks = 5;
    n1WarnRpm = 0.0f; n2WarnRpm = 22000.0f; totWarnC = 0.0f; oilWarnBar = 0.0f;  // n1 0 = auto (rpmLimit*0.9)
    clusterEnabled = true;
    rcFailsafeMs = 500;
    governorTargetRpm = 0.0f; governorBandRpm = 500.0f;
    governorKp = 0.001f; governorPitchKp = 0.0005f; governorPitchRampSec = 10.0f;
    glowPreheatMs = 10000; glowPreheatMaxPct = 80.0f; glowHoldPct = 30.0f; glowWaitUntilHot = false;
    throttleMinRaw = 0; throttleMaxRaw = 4095;
    idleMinRaw = 0; idleMaxRaw = 4095; flameThreshold = 500;
    oilPolyA = 0; oilPolyB = 0; oilPolyC = 0; oilPolyD = 0;
    oilPolyXMin = 0; oilPolyXMax = 4095;
    p1RawMin = 0; p1RawMax = 4095; p1ValMax = 10.0f;
    p2RawMin = 0; p2RawMax = 4095; p2ValMax = 10.0f;
    fuelPressRawMin = 0; fuelPressRawMax = 4095; fuelPressValMax = 10.0f;
    fuelFlowRawMin = 0; fuelFlowRawMax = 4095; fuelFlowValMax = 10.0f;
    sessionLogMask = SLOG_DEFAULT; sessionLogIntervalMs = 1000;
    ruleCount = 0;
    for (int i = 0; i < MAX_RULES; i++) rules[i] = {};
    loadWarning[0] = '\0';
    // Runtime stats are NOT reset here; hour meter data persists across config reloads.
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
        if (!doc[sec].is<JsonObjectConst>()) {
            _missingRequiredSections = true;
            // Do not spam UART from the boot task for every absent section:
            // on some USB/serial host states that can block long enough to trip
            // the interrupt watchdog. Config::load() emits one repair message
            // and saves a completed file after _fromDoc().
        }
    }

    // UI theme (cosmetic; the browser falls back to the default for unknown keys)
    { const char* th = doc["ui_theme"] | "";
      if (th[0]) { strncpy(uiTheme, th, sizeof(uiTheme) - 1); uiTheme[sizeof(uiTheme) - 1] = '\0'; } }

    auto eng = doc["engine"];
    rpmLimit          = eng["rpm_limit"]          | rpmLimit;
    n2RpmLimit        = eng["n2_rpm_limit"]       | n2RpmLimit;
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
    fuelPumpMinPct      = th["fuel_pump_min_pct"] | fuelPumpMinPct;
    throttleIdleMaxPct  = th["idle_max_pct"] | throttleIdleMaxPct;
    throttleExpo        = th["expo"]         | throttleExpo;
    if (!th["pullback_n1"].isNull()) pullbackN1Enabled = th["pullback_n1"].as<bool>();
    if (!th["pullback_n2"].isNull()) pullbackN2Enabled = th["pullback_n2"].as<bool>();
    if (!th["pullback_egt"].isNull()) pullbackEgtEnabled = th["pullback_egt"].as<bool>();
    pullbackN1SoftRpm = th["pullback_n1_soft_rpm"] | pullbackN1SoftRpm;
    pullbackN1HardRpm = th["pullback_n1_hard_rpm"] | pullbackN1HardRpm;
    pullbackN2SoftRpm = th["pullback_n2_soft_rpm"] | pullbackN2SoftRpm;
    pullbackN2HardRpm = th["pullback_n2_hard_rpm"] | pullbackN2HardRpm;
    pullbackEgtSoftC = th["pullback_egt_soft_c"] | pullbackEgtSoftC;
    pullbackEgtHardC = th["pullback_egt_hard_c"] | pullbackEgtHardC;
    pullbackMinThrottlePct = th["pullback_min_pct"] | pullbackMinThrottlePct;
    pullbackStrength = th["pullback_strength"] | pullbackStrength;
    rpmLimiterMode            = th["rpm_limiter_mode"]              | rpmLimiterMode;
    pullbackLookaheadMs       = th["pullback_lookahead_ms"]         | pullbackLookaheadMs;
    pullbackNearLimitRampUpMs = th["pullback_near_limit_rampup_ms"] | pullbackNearLimitRampUpMs;
    pullbackApproachZoneRpm   = th["pullback_approach_zone_rpm"]    | pullbackApproachZoneRpm;
    rpmAccelFilter            = th["rpm_accel_filter"]              | rpmAccelFilter;

    auto di = doc["dynamic_idle"];
    idleTargetRpm    = di["target_rpm"]    | idleTargetRpm;
    idleRampUpMs     = di["ramp_up_ms"]    | idleRampUpMs;
    idleRampDownMs   = di["ramp_down_ms"]  | idleRampDownMs;
    idleDeadbandRpm  = di["deadband_rpm"]  | idleDeadbandRpm;
    idleRpmLimit     = di["rpm_limit"]     | idleRpmLimit;
    idleMinMultiplier= di["min_multiplier"]| idleMinMultiplier;
    idleMaxMultiplier= di["max_multiplier"]| idleMaxMultiplier;
    if (!di["use_n2"].isNull()) idleUseN2 = di["use_n2"].as<bool>();
    idleIGain        = di["i_gain"]        | idleIGain;
    idleIMax         = di["i_max"]         | idleIMax;
    idleMode              = di["idle_mode"]         | idleMode;
    idleDecelEnterRpm     = di["decel_enter_rpm"]   | idleDecelEnterRpm;
    idleDecelDropPct      = di["decel_drop_pct"]    | idleDecelDropPct;
    idleLookaheadMs       = di["lookahead_ms"]      | idleLookaheadMs;
    idleSettleBandRpm     = di["settle_band_rpm"]   | idleSettleBandRpm;
    idleFullResponseRpm   = di["full_response_rpm"] | idleFullResponseRpm;
    idleTrimUpPctPerSec   = di["trim_up_pct_s"]     | idleTrimUpPctPerSec;
    idleTrimDownPctPerSec = di["trim_down_pct_s"]   | idleTrimDownPctPerSec;
    idleLearnRate         = di["learn_rate"]        | idleLearnRate;
    idleLearnAccelMax     = di["learn_accel_max"]   | idleLearnAccelMax;

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
    fuelPressRawMin  = cal["fuel_press_raw_min"] | fuelPressRawMin;
    fuelPressRawMax  = cal["fuel_press_raw_max"] | fuelPressRawMax;
    fuelPressValMax  = cal["fuel_press_val_max"] | fuelPressValMax;
    fuelFlowRawMin   = cal["fuel_flow_raw_min"]  | fuelFlowRawMin;
    fuelFlowRawMax   = cal["fuel_flow_raw_max"]  | fuelFlowRawMax;
    fuelFlowValMax   = cal["fuel_flow_val_max"]  | fuelFlowValMax;

    auto rl = doc["relight"];
    if (!rl["enabled"].isNull()) relightEnabled = rl["enabled"].as<bool>();
    relightIgnitionTarget = rl["ignition_target"] | relightIgnitionTarget;
    relightConfirmSource = rl["confirm_source"]   | relightConfirmSource;
    relightMinRpm     = rl["min_rpm"]          | relightMinRpm;
    relightConfirmRpm = rl["confirm_rpm"]      | relightConfirmRpm;
    relightTotRiseC   = rl["tot_rise_c"]       | relightTotRiseC;
    relightTimeoutMs  = rl["relight_timeout_ms"] | relightTimeoutMs;

    auto tl = doc["tools"];
    toolFuelPrimeMs   = tl["fuel_prime_ms"]   | toolFuelPrimeMs;
    toolOilPrimeMs    = tl["oil_prime_ms"]    | toolOilPrimeMs;
    toolIgnTestMs     = tl["ign_test_ms"]     | toolIgnTestMs;
    toolIgn2TestMs    = tl["ign2_test_ms"]    | toolIgn2TestMs;
    toolGlowTestMs    = tl["glow_test_ms"]    | toolGlowTestMs;
    toolGlowTestPct   = tl["glow_test_pct"]   | toolGlowTestPct;
    toolStartTestMs   = tl["start_test_ms"]   | toolStartTestMs;
    toolStartTestPct  = tl["start_test_pct"]  | toolStartTestPct;
    toolFuelSolTestMs = tl["fuel_sol_test_ms"]| toolFuelSolTestMs;
    toolIdleTestMs    = tl["idle_test_ms"]    | toolIdleTestMs;
    toolOilScavTestMs = tl["oil_scav_test_ms"]| toolOilScavTestMs;
    toolCoolFanTestMs = tl["cool_fan_test_ms"]| toolCoolFanTestMs;
    toolAirstarterTestMs = tl["airstarter_test_ms"] | toolAirstarterTestMs;
    toolBleedValveTestMs = tl["bleed_valve_test_ms"] | toolBleedValveTestMs;
    toolFuelPump2TestMs = tl["fuel_pump2_test_ms"] | toolFuelPump2TestMs;
    toolFuelPump2TestPct = tl["fuel_pump2_test_pct"] | toolFuelPump2TestPct;
    toolAbSolTestMs = tl["ab_sol_test_ms"] | toolAbSolTestMs;
    toolAbPumpTestMs = tl["ab_pump_test_ms"] | toolAbPumpTestMs;
    toolAbPumpTestPct = tl["ab_pump_test_pct"] | toolAbPumpTestPct;
    toolStarterEnTestMs = tl["starter_en_test_ms"] | toolStarterEnTestMs;
    toolPropPitchTestMs = tl["prop_pitch_test_ms"] | toolPropPitchTestMs;
    toolPropPitchTestPct = tl["prop_pitch_test_pct"] | toolPropPitchTestPct;

    auto tm = doc["telemetry"];
    wsIntervalMs       = tm["ws_interval_ms"]       | wsIntervalMs;
    snapshotIntervalMs = tm["snapshot_interval_ms"] | snapshotIntervalMs;
    controlLoopHz      = tm["control_loop_hz"]      | controlLoopHz;
    if (!tm["log_standby"].isNull()) logStandby = tm["log_standby"].as<bool>();

    auto sa = doc["starter_control"];
    starterLowRpmSupportPct = sa["low_rpm_support_pct"] | starterLowRpmSupportPct;
    starterLowRpmSupportDisengageRpm = sa["low_rpm_support_disengage_rpm"] | starterLowRpmSupportDisengageRpm;
    starterStartupRampPctPerSec = sa["startup_ramp_pct_per_s"] | starterStartupRampPctPerSec;

    auto oilx = doc["oil_advanced"];
    oilZeroBar          = oilx["zero_bar"]      | oilZeroBar;
    oilPressureDeadband = oilx["deadband_bar"]  | oilPressureDeadband;

    auto sob = doc["standby_oil"];
    standbyOilSource   = sob["source"]    | standbyOilSource;
    standbyOilRpmLimit = sob["rpm_limit"] | standbyOilRpmLimit;
    standbyOilFeedPct  = sob["feed_pct"]  | standbyOilFeedPct;
    standbyOilFeedBar  = sob["feed_bar"]  | standbyOilFeedBar;

    auto limp = doc["limp_mode"];
    limpMaxThrottlePct = limp["max_throttle_pct"] | limpMaxThrottlePct;

    auto misc = doc["misc"];
    cooldownSkipHoldMs = misc["cooldown_skip_hold_ms"] | cooldownSkipHoldMs;
    if (!misc["igniter_on_start"].isNull()) igniterOnStart = misc["igniter_on_start"].as<bool>();
    manualRelightIgnitionTarget = misc["igniter_on_start_target"] | manualRelightIgnitionTarget;

    auto rh = doc["rpm_health"];
    rpmJumpThreshold  = rh["jump_threshold"]   | rpmJumpThreshold;
    rpmZeroStuckTicks = rh["zero_stuck_ticks"] | rpmZeroStuckTicks;

    auto cl = doc["cluster"];
    n1WarnRpm      = cl["n1_warn_rpm"]  | n1WarnRpm;
    n2WarnRpm      = cl["n2_warn_rpm"]  | n2WarnRpm;
    totWarnC       = cl["tot_warn_c"]   | totWarnC;
    oilWarnBar     = cl["oil_warn_bar"] | oilWarnBar;
    if (!cl["enabled"].isNull()) clusterEnabled = cl["enabled"].as<bool>();

    auto rc = doc["rc_input"];
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
    abLightupPumpPct     = ab["lightup_pump_pct"]    | abLightupPumpPct;
    abPumpMinPct         = ab["pump_min_pct"]        | abPumpMinPct;
    abPumpMaxPct         = ab["pump_max_pct"]        | abPumpMaxPct;
    abPumpControlMode  = ab["pump_control_mode"]    | abPumpControlMode;
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
        if (sl["oil_temp"] | false) mask |= SLOG_OIL_TEMP;
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
        if (sl["wet_glow"]   | false) mask |= SLOG_WET_GLOW;
        if (sl["glow_current"] | false) mask |= SLOG_GLOW_CURRENT;
        if (sl["ign_current"]  | false) mask |= SLOG_IGN_CURRENT;
        if (sl["ign2_current"] | false) mask |= SLOG_IGN2_CURRENT;
        if (sl["oil_current"]  | false) mask |= SLOG_OIL_CURRENT;
        if (sl["fp2"]        | false) mask |= SLOG_FP2;
        if (sl["ab"]         | false) mask |= SLOG_AB;
        if (sl["prop"]       | false) mask |= SLOG_PROP;
        if (sl["oil_pct"]    | false) mask |= SLOG_OIL_PCT;
        if (sl["loop"]       | false) mask |= SLOG_LOOP;
        sessionLogMask = mask;
        sessionLogIntervalMs = sl["interval_ms"] | sessionLogIntervalMs;
    }

    auto stats = doc["stats"];
    if (!stats.isNull()) {
        // Read the file values first (ArduinoJson lookups), then take the mux
        // only for the compare-assign so the critical section stays tiny. A
        // missing key reads as 0, which never beats the running counter.
        uint32_t fileRunSeconds    = stats["total_run_seconds"]   | 0u;
        uint32_t fileStartAttempts = stats["start_attempt_count"] | 0u;
        uint32_t fileRuns          = stats["run_count"]           | 0u;
        portENTER_CRITICAL(&s_statsMux);
        if (fileRunSeconds    > totalRunSeconds)   totalRunSeconds   = fileRunSeconds;
        if (fileStartAttempts > startAttemptCount) startAttemptCount = fileStartAttempts;
        if (fileRuns          > runCount)          runCount          = fileRuns;
        portEXIT_CRITICAL(&s_statsMux);
    }

    // ── Automation rules ──────────────────────────────────────────
    auto rulesArr = doc["rules"];
    ruleCount = 0;
    for (int i = 0; i < MAX_RULES; i++) rules[i] = {};
    if (!rulesArr.isNull() && rulesArr.is<JsonArrayConst>()) {
        for (JsonObjectConst jr : rulesArr.as<JsonArrayConst>()) {
            if (ruleCount >= MAX_RULES) break;
            Rule& r = rules[ruleCount++];
            r.enabled   = jr["enabled"]   | false;
            r.kind      = (uint8_t)(jr["kind"]      | 0);
            r.op        = (uint8_t)(jr["op"]        | 0);
            r.threshold = jr["threshold"] | 0.0f;
            r.onValue   = jr["on_value"]  | 1.0f;
            r.offValue  = jr["off_value"] | 0.0f;
            r.hysteresis= jr["hysteresis"] | 0.0f;
            r.inputMin  = jr["input_min"]  | 0.0f;
            r.inputMax  = jr["input_max"]  | 1.0f;
            r.outputMin = jr["output_min"] | 0.0f;
            r.outputMax = jr["output_max"] | 1.0f;
            r.modeMask  = (uint8_t)(jr["mode_mask"] | 0x0F);
            const char* n = jr["name"] | "";
            strncpy(r.name, n, sizeof(r.name) - 1);
            r.name[sizeof(r.name) - 1] = '\0';
            const char* source = jr["source"] | "";
            const char* target = jr["target"] | "";
            strlcpy(r.sourceId, source, sizeof(r.sourceId));
            strlcpy(r.targetId, target, sizeof(r.targetId));
            int8_t sourceHandle = ruleSourceHandle(r.sourceId);
            int8_t targetHandle = ruleTargetHandle(r.targetId);
            if (sourceHandle < 0 || targetHandle < 0) r.enabled = false;
            else { r.sensor = (uint8_t)sourceHandle; r.actuator = (uint8_t)targetHandle; }
        }
    }

    // Structurally broken values (NaN, negative where nonsensical) fall back
    // to defaults; out-of-range-HIGH safety limits are accepted and warned
    // about below instead (never block the informed user).
    if (!isfinite(rpmLimit) || rpmLimit <= 0.0f) rpmLimit = 100000.0f;
    if (!isfinite(n2RpmLimit) || n2RpmLimit < 0.0f) n2RpmLimit = 0.0f;
    if (minRpm <= 0.0f) minRpm = 30000.0f;
    if (minRpm >= rpmLimit) minRpm = rpmLimit * 0.3f;
    if (!isfinite(totLimit) || totLimit < 0.0f) totLimit = 750.0f;
    if (totCooldownTarget < 0.0f) totCooldownTarget = 0.0f;
    totSafeMargin = totLimit > 0.0f
        ? constrain(totSafeMargin, 0.0f, totLimit)
        : max(0.0f, totSafeMargin);
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
    relightIgnitionTarget = constrain(relightIgnitionTarget, 0, 2);
    relightConfirmSource = constrain(relightConfirmSource, 0, 3);
    if (relightMinRpm < 0.0f) relightMinRpm = 0.0f;
    if (relightConfirmRpm < 0.0f) relightConfirmRpm = 0.0f;
    if (relightTotRiseC < 0.0f) relightTotRiseC = 0.0f;
    if (starterLowRpmSupportDisengageRpm < 0.0f) starterLowRpmSupportDisengageRpm = 0.0f;
    if (starterStartupRampPctPerSec < 0.0f) starterStartupRampPctPerSec = 0.0f;
    standbyOilSource = constrain(standbyOilSource, 0, 2);
    manualRelightIgnitionTarget = constrain(manualRelightIgnitionTarget, 0, 2);
    for (int i = 0; i < ruleCount; i++) {
        if (rules[i].sensor > 26 && !ChannelRegistry::isInputSensor(rules[i].sensor))
            rules[i].enabled = false;
        rules[i].kind = constrain(rules[i].kind, 0, 1);
        rules[i].op = constrain(rules[i].op, 0, 1);
        if (rules[i].actuator > 17 && !ChannelRegistry::isOutputActuator(rules[i].actuator))
            rules[i].enabled = false;
        if (rules[i].hysteresis < 0.0f) rules[i].hysteresis = 0.0f;
        rules[i].onValue = constrain(rules[i].onValue, 0.0f, 1.0f);
        rules[i].offValue = constrain(rules[i].offValue, 0.0f, 1.0f);
        rules[i].outputMin = constrain(rules[i].outputMin, 0.0f, 1.0f);
        rules[i].outputMax = constrain(rules[i].outputMax, 0.0f, 1.0f);
        if (!isfinite(rules[i].inputMin)) rules[i].inputMin = 0.0f;
        if (!isfinite(rules[i].inputMax) || rules[i].inputMax <= rules[i].inputMin) rules[i].inputMax = rules[i].inputMin + 1.0f;
        rules[i].modeMask &= 0x0F;
        if (rules[i].modeMask == 0) rules[i].enabled = false;
    }
    if (standbyOilRpmLimit < 0.0f) standbyOilRpmLimit = 0.0f;
    auto clampToolMs = [](uint32_t& value, uint32_t fallback, uint32_t minMs) {
        if (value < minMs || value > 60000u) value = fallback;
    };
    clampToolMs(toolFuelPrimeMs, 3000u, 100u);
    clampToolMs(toolOilPrimeMs, 5000u, 100u);
    clampToolMs(toolIgnTestMs, 2000u, 100u);
    clampToolMs(toolIgn2TestMs, 2000u, 100u);
    clampToolMs(toolGlowTestMs, 10000u, 100u);
    toolGlowTestPct = constrain(toolGlowTestPct, 0.0f, 100.0f);
    clampToolMs(toolStartTestMs, 2000u, 100u);
    toolStartTestPct = constrain(toolStartTestPct, 0.0f, 100.0f);
    clampToolMs(toolFuelSolTestMs, 1000u, 50u);
    clampToolMs(toolIdleTestMs, 3000u, 100u);
    clampToolMs(toolOilScavTestMs, 2000u, 100u);
    clampToolMs(toolCoolFanTestMs, 3000u, 100u);
    clampToolMs(toolAirstarterTestMs, 1000u, 50u);
    clampToolMs(toolBleedValveTestMs, 1000u, 50u);
    clampToolMs(toolFuelPump2TestMs, 3000u, 100u);
    toolFuelPump2TestPct = constrain(toolFuelPump2TestPct, 0.0f, 100.0f);
    clampToolMs(toolAbSolTestMs, 1000u, 50u);
    clampToolMs(toolAbPumpTestMs, 2000u, 100u);
    toolAbPumpTestPct = constrain(toolAbPumpTestPct, 0.0f, 100.0f);
    clampToolMs(toolStarterEnTestMs, 1000u, 50u);
    clampToolMs(toolPropPitchTestMs, 3000u, 100u);
    toolPropPitchTestPct = constrain(toolPropPitchTestPct, 0.0f, 100.0f);
    // These bounds mirror the PATCH validator in validateJson (telemetry group)
    // so an accepted value survives a reboot unchanged — keep the two in sync.
    if (wsIntervalMs < 333u || wsIntervalMs > 60000u) wsIntervalMs = 333u;
    if (snapshotIntervalMs < 500u || snapshotIntervalMs > 3600000u) snapshotIntervalMs = 10000u;
    if (controlLoopHz < 50u || controlLoopHz > 1000u) controlLoopHz = 400u;
    if (sessionLogIntervalMs < 100u || sessionLogIntervalMs > 60000u) sessionLogIntervalMs = 1000u;
    if (cooldownSkipHoldMs < 0) cooldownSkipHoldMs = 0;
    if (fp2RampMs < 0) fp2RampMs = 0;
    if (govHoldTimeoutMs < 0) govHoldTimeoutMs = 0;
    starterDemand = constrain(starterDemand, 0.0f, 100.0f);
    throttleSetPct = constrain(throttleSetPct, 0.0f, 100.0f);
    oilPumpOnPct = constrain(oilPumpOnPct, 0.0f, 100.0f);
    cooldownStarterPct = constrain(cooldownStarterPct, 0.0f, 100.0f);
    cooldownOilPct = constrain(cooldownOilPct, 0.0f, 100.0f);
    fuelPumpMinPct     = constrain(fuelPumpMinPct, 0.0f, 100.0f);
    throttleIdleMaxPct = constrain(throttleIdleMaxPct, fuelPumpMinPct, 100.0f);
    throttleExpo = constrain(throttleExpo, 0.0f, 1.0f);
    pullbackN1SoftRpm = constrain(pullbackN1SoftRpm, 0.0f, 500000.0f);
    pullbackN1HardRpm = constrain(pullbackN1HardRpm, 0.0f, 500000.0f);
    if (pullbackN1HardRpm > 0.0f && pullbackN1HardRpm <= pullbackN1SoftRpm) pullbackN1HardRpm = pullbackN1SoftRpm + 1.0f;
    pullbackN2SoftRpm = constrain(pullbackN2SoftRpm, 0.0f, 500000.0f);
    pullbackN2HardRpm = constrain(pullbackN2HardRpm, 0.0f, 500000.0f);
    if (pullbackN2HardRpm > 0.0f && pullbackN2HardRpm <= pullbackN2SoftRpm) pullbackN2HardRpm = pullbackN2SoftRpm + 1.0f;
    pullbackEgtSoftC = constrain(pullbackEgtSoftC, 0.0f, 1400.0f);
    pullbackEgtHardC = constrain(pullbackEgtHardC, 0.0f, 1400.0f);
    if (pullbackEgtHardC > 0.0f && pullbackEgtHardC <= pullbackEgtSoftC) pullbackEgtHardC = pullbackEgtSoftC + 1.0f;
    pullbackMinThrottlePct = constrain(pullbackMinThrottlePct, 0.0f, 100.0f);
    pullbackStrength = constrain(pullbackStrength, 0.0f, 5.0f);
    rpmLimiterMode = constrain(rpmLimiterMode, 0, 1);
    pullbackLookaheadMs = constrain(pullbackLookaheadMs, 0.0f, 5000.0f);
    pullbackNearLimitRampUpMs = constrain(pullbackNearLimitRampUpMs, 0.0f, 20000.0f);
    if (pullbackApproachZoneRpm < 0.0f) pullbackApproachZoneRpm = 0.0f;
    rpmAccelFilter = constrain(rpmAccelFilter, 0.02f, 1.0f);
    if (idleTargetRpm < 0.0f) idleTargetRpm = 0.0f;
    if (idleDeadbandRpm < 0.0f) idleDeadbandRpm = 0.0f;
    if (idleRpmLimit < 0.0f) idleRpmLimit = 0.0f;
    idleMinMultiplier = constrain(idleMinMultiplier, 0.0f, 1.0f);
    idleMaxMultiplier = constrain(idleMaxMultiplier, 1.0f, 3.0f);
    idleIGain = constrain(idleIGain, 0.0f, 2.0f);
    idleIMax = constrain(idleIMax, 0.0f, 0.5f);
    idleMode = constrain(idleMode, 0, 1);
    if (idleDecelEnterRpm < 0.0f) idleDecelEnterRpm = 0.0f;
    idleDecelDropPct = constrain(idleDecelDropPct, 0.0f, 50.0f);
    idleLookaheadMs = constrain(idleLookaheadMs, 0.0f, 5000.0f);
    if (idleSettleBandRpm < 0.0f) idleSettleBandRpm = 0.0f;
    if (idleFullResponseRpm < 1.0f) idleFullResponseRpm = 1.0f;
    idleTrimUpPctPerSec = constrain(idleTrimUpPctPerSec, 0.0f, 50.0f);
    idleTrimDownPctPerSec = constrain(idleTrimDownPctPerSec, 0.0f, 50.0f);
    idleLearnRate = constrain(idleLearnRate, 0.0f, 1.0f);
    if (idleLearnAccelMax < 0.0f) idleLearnAccelMax = 0.0f;
    glowPreheatMaxPct = constrain(glowPreheatMaxPct, 0.0f, 100.0f);
    glowHoldPct = constrain(glowHoldPct, 0.0f, 100.0f);
    starterLowRpmSupportPct = constrain(starterLowRpmSupportPct, 0.0f, 100.0f);
    standbyOilFeedPct = constrain(standbyOilFeedPct, 0.0f, 100.0f);
    standbyOilFeedBar = constrain(standbyOilFeedBar, 0.0f, 20.0f);
    if (modifiedIdleMultiplier < 0.0f) modifiedIdleMultiplier = 0.0f;
    fp2StartPct = constrain(fp2StartPct, 0.0f, 100.0f);
    fp2EndPct = constrain(fp2EndPct, 0.0f, 100.0f);
    fp2DemandPct = constrain(fp2DemandPct, 0.0f, 100.0f);
    if (oilFailsafeDelayMs < 0) oilFailsafeDelayMs = 0;
    oilFailsafePct = constrain(oilFailsafePct, 0.0f, 100.0f);
    if (totRiseRateLimitDegPerSec < 0.0f) totRiseRateLimitDegPerSec = 0.0f;
    if (!isfinite(titLimit) || titLimit < 0.0f) titLimit = 0.0f;
    if (!isfinite(oilTempLimit) || oilTempLimit < 0.0f) oilTempLimit = 0.0f;
    if (fuelPressMin < 0.0f) fuelPressMin = 0.0f;
    if (battVoltMin < 0.0f) battVoltMin = 0.0f;
    if (surgeDetectRpmVariance < 0.0f) surgeDetectRpmVariance = 0.0f;
    // jump_threshold <= 0 would flag every RPM change as a JUMP fault
    if (!isfinite(rpmJumpThreshold) || rpmJumpThreshold <= 0.0f) rpmJumpThreshold = 0.40f;
    if (rpmZeroStuckTicks < 1) rpmZeroStuckTicks = 1;
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
    abLightupPumpPct = constrain(abLightupPumpPct, 0.0f, 100.0f);
    abPumpMinPct = constrain(abPumpMinPct, 0.0f, 100.0f);
    abPumpMaxPct = constrain(abPumpMaxPct, abPumpMinPct, 100.0f);
    abPumpControlMode = constrain(abPumpControlMode, 0, 2);
    abMainFuelOffsetPct = constrain(abMainFuelOffsetPct, -20.0f, 50.0f);
    limpMaxThrottlePct = constrain(limpMaxThrottlePct, 0.0f, 100.0f);
    governorKp = constrain(governorKp, 0.0f, 0.01f);
    governorPitchKp = constrain(governorPitchKp, 0.0f, 0.01f);
    if (governorTargetRpm < 0.0f) governorTargetRpm = 0.0f;
    if (governorBandRpm < 0.0f) governorBandRpm = 0.0f;
    if (governorPitchRampSec < 0.0f) governorPitchRampSec = 1.0f;

    // ── Accept + warn ─────────────────────────────────────────────
    // Safety-relevant values beyond the recommended caps load as-is but
    // raise a persistent dashboard notice (telemetry "config_load_warning")
    // and a flight-log marker. Recomputed on every load/upload so the
    // notice clears once the value is fixed.
    loadWarning[0] = '\0';
    char warnBuf[96];
    auto appendLoadWarning = [](const char* msg) {
        size_t used = strlen(loadWarning);
        snprintf(loadWarning + used, sizeof(loadWarning) - used, "%s%s",
                 used ? "; " : "", msg);
        Serial.printf("[Config] WARNING: %s\n", msg);
    };
    auto warnHighLimit = [&](const char* name, float value, float cap) {
        snprintf(warnBuf, sizeof(warnBuf), "%s %.0f exceeds recommended max %.0f",
                 name, value, cap);
        appendLoadWarning(warnBuf);
    };
    if (rpmLimit > 500000.0f)  warnHighLimit("rpm_limit", rpmLimit, 500000.0f);
    if (n2RpmLimit > 500000.0f) warnHighLimit("n2_rpm_limit", n2RpmLimit, 500000.0f);
    if (totLimit > 1400.0f)    warnHighLimit("tot_limit", totLimit, 1400.0f);
    if (titLimit > 1400.0f)    warnHighLimit("tit_limit_c", titLimit, 1400.0f);
    if (oilTempLimit > 300.0f) warnHighLimit("oil_temp_limit_c", oilTempLimit, 300.0f);
    if (totLimit > 0.0f && totCooldownTarget >= totLimit) {
        snprintf(warnBuf, sizeof(warnBuf),
                 "tot_cooldown_target %.0f is not below tot_limit %.0f - cooldown ends immediately",
                 totCooldownTarget, totLimit);
        appendLoadWarning(warnBuf);
    }
    // Oil target (oilMapMin) below the low-oil fault (oilRunningMin) makes the
    // pump aim beneath the shutdown line -> nuisance low-oil trips.
    if (oilRunningMin > 0.0f && oilMapMin > 0.0f && oilMapMin < oilRunningMin) {
        snprintf(warnBuf, sizeof(warnBuf),
                 "oil target %.1f is below low-oil fault %.1f bar - nuisance shutdowns likely",
                 (double)oilMapMin, (double)oilRunningMin);
        appendLoadWarning(warnBuf);
    }

    sanitizeForHardware();
    // Rules were reloaded (and possibly compacted) — stale hysteresis latch
    // state must not carry over to a different rule at the same index.
    RulesEngine::resetLatches();
}

void Config::_toDoc(JsonDocument& doc) {
    sanitizeForHardware();
    doc["profile_id"]     = HardwareConfig::profileId[0] ? HardwareConfig::profileId : OT_PROFILE_ID;
    doc["config_version"] = CONFIG_VERSION;
    doc["ui_theme"]       = uiTheme;

    auto eng = doc["engine"].to<JsonObject>();
    eng["rpm_limit"]          = rpmLimit;
    eng["n2_rpm_limit"]       = n2RpmLimit;
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
    th["fuel_pump_min_pct"] = fuelPumpMinPct;
    th["idle_max_pct"] = throttleIdleMaxPct;
    th["expo"]         = throttleExpo;
    th["pullback_n1"] = pullbackN1Enabled;
    th["pullback_n2"] = pullbackN2Enabled;
    th["pullback_egt"] = pullbackEgtEnabled;
    th["pullback_n1_soft_rpm"] = pullbackN1SoftRpm;
    th["pullback_n1_hard_rpm"] = pullbackN1HardRpm;
    th["pullback_n2_soft_rpm"] = pullbackN2SoftRpm;
    th["pullback_n2_hard_rpm"] = pullbackN2HardRpm;
    th["pullback_egt_soft_c"] = pullbackEgtSoftC;
    th["pullback_egt_hard_c"] = pullbackEgtHardC;
    th["pullback_min_pct"] = pullbackMinThrottlePct;
    th["pullback_strength"] = pullbackStrength;
    th["rpm_limiter_mode"]              = rpmLimiterMode;
    th["pullback_lookahead_ms"]         = pullbackLookaheadMs;
    th["pullback_near_limit_rampup_ms"] = pullbackNearLimitRampUpMs;
    th["pullback_approach_zone_rpm"]    = pullbackApproachZoneRpm;
    th["rpm_accel_filter"]              = rpmAccelFilter;

    auto di = doc["dynamic_idle"].to<JsonObject>();
    di["target_rpm"]    = idleTargetRpm;
    di["ramp_up_ms"]    = idleRampUpMs;
    di["ramp_down_ms"]  = idleRampDownMs;
    di["deadband_rpm"]  = idleDeadbandRpm;
    di["rpm_limit"]     = idleRpmLimit;
    di["min_multiplier"]= idleMinMultiplier;
    di["max_multiplier"]= idleMaxMultiplier;
    di["use_n2"]        = idleUseN2;
    di["i_gain"]        = idleIGain;
    di["i_max"]         = idleIMax;
    di["idle_mode"]           = idleMode;
    di["decel_enter_rpm"]     = idleDecelEnterRpm;
    di["decel_drop_pct"]      = idleDecelDropPct;
    di["lookahead_ms"]        = idleLookaheadMs;
    di["settle_band_rpm"]     = idleSettleBandRpm;
    di["full_response_rpm"]   = idleFullResponseRpm;
    di["trim_up_pct_s"]       = idleTrimUpPctPerSec;
    di["trim_down_pct_s"]     = idleTrimDownPctPerSec;
    di["learn_rate"]          = idleLearnRate;
    di["learn_accel_max"]     = idleLearnAccelMax;

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
    cal["fuel_press_raw_min"] = fuelPressRawMin;
    cal["fuel_press_raw_max"] = fuelPressRawMax;
    cal["fuel_press_val_max"] = fuelPressValMax;
    cal["fuel_flow_raw_min"]  = fuelFlowRawMin;
    cal["fuel_flow_raw_max"]  = fuelFlowRawMax;
    cal["fuel_flow_val_max"]  = fuelFlowValMax;

    auto rl = doc["relight"].to<JsonObject>();
    rl["enabled"]            = relightEnabled;
    rl["ignition_target"]    = relightIgnitionTarget;
    rl["confirm_source"]     = relightConfirmSource;
    rl["min_rpm"]            = relightMinRpm;
    rl["confirm_rpm"]        = relightConfirmRpm;
    rl["tot_rise_c"]         = relightTotRiseC;
    rl["relight_timeout_ms"] = relightTimeoutMs;

    auto tl = doc["tools"].to<JsonObject>();
    tl["fuel_prime_ms"]    = toolFuelPrimeMs;
    tl["oil_prime_ms"]     = toolOilPrimeMs;
    tl["ign_test_ms"]      = toolIgnTestMs;
    tl["ign2_test_ms"]     = toolIgn2TestMs;
    tl["glow_test_ms"]     = toolGlowTestMs;
    tl["glow_test_pct"]    = toolGlowTestPct;
    tl["start_test_ms"]    = toolStartTestMs;
    tl["start_test_pct"]   = toolStartTestPct;
    tl["fuel_sol_test_ms"] = toolFuelSolTestMs;
    tl["idle_test_ms"]     = toolIdleTestMs;
    tl["oil_scav_test_ms"] = toolOilScavTestMs;
    tl["cool_fan_test_ms"] = toolCoolFanTestMs;
    tl["airstarter_test_ms"] = toolAirstarterTestMs;
    tl["bleed_valve_test_ms"] = toolBleedValveTestMs;
    tl["fuel_pump2_test_ms"] = toolFuelPump2TestMs;
    tl["fuel_pump2_test_pct"] = toolFuelPump2TestPct;
    tl["ab_sol_test_ms"] = toolAbSolTestMs;
    tl["ab_pump_test_ms"] = toolAbPumpTestMs;
    tl["ab_pump_test_pct"] = toolAbPumpTestPct;
    tl["starter_en_test_ms"] = toolStarterEnTestMs;
    tl["prop_pitch_test_ms"] = toolPropPitchTestMs;
    tl["prop_pitch_test_pct"] = toolPropPitchTestPct;

    auto tm = doc["telemetry"].to<JsonObject>();
    tm["ws_interval_ms"]       = wsIntervalMs;
    tm["snapshot_interval_ms"] = snapshotIntervalMs;
    tm["control_loop_hz"]      = controlLoopHz;
    tm["log_standby"]          = logStandby;

    auto sa = doc["starter_control"].to<JsonObject>();
    sa["low_rpm_support_pct"] = starterLowRpmSupportPct;
    sa["low_rpm_support_disengage_rpm"] = starterLowRpmSupportDisengageRpm;
    sa["startup_ramp_pct_per_s"] = starterStartupRampPctPerSec;

    auto oilx = doc["oil_advanced"].to<JsonObject>();
    oilx["zero_bar"]     = oilZeroBar;
    oilx["deadband_bar"] = oilPressureDeadband;


    auto sob = doc["standby_oil"].to<JsonObject>();
    sob["source"]    = standbyOilSource;
    sob["rpm_limit"] = standbyOilRpmLimit;
    sob["feed_pct"]  = standbyOilFeedPct;
    sob["feed_bar"]  = standbyOilFeedBar;

    auto limp = doc["limp_mode"].to<JsonObject>();
    limp["max_throttle_pct"] = limpMaxThrottlePct;

    auto misc = doc["misc"].to<JsonObject>();
    misc["cooldown_skip_hold_ms"] = cooldownSkipHoldMs;
    misc["igniter_on_start"]      = igniterOnStart;
    misc["igniter_on_start_target"] = manualRelightIgnitionTarget;


    auto rh = doc["rpm_health"].to<JsonObject>();
    rh["jump_threshold"]   = rpmJumpThreshold;
    rh["zero_stuck_ticks"] = rpmZeroStuckTicks;

    auto cl = doc["cluster"].to<JsonObject>();
    cl["n1_warn_rpm"]  = n1WarnRpm;
    cl["n2_warn_rpm"]  = n2WarnRpm;
    cl["tot_warn_c"]   = totWarnC;
    cl["oil_warn_bar"] = oilWarnBar;
    cl["enabled"]      = clusterEnabled;

    auto rc = doc["rc_input"].to<JsonObject>();
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
    ab["lightup_pump_pct"]     = abLightupPumpPct;
    ab["pump_min_pct"]         = abPumpMinPct;
    ab["pump_max_pct"]         = abPumpMaxPct;
    ab["pump_control_mode"]    = abPumpControlMode;
    ab["main_fuel_offset_pct"] = abMainFuelOffsetPct;
    ab["stabilize_ms"]         = abStabilizeMs;
    ab["stabilize_max_tot"]    = abStabilizeMaxTot;

    auto sl = doc["session_log"].to<JsonObject>();
    sl["n1"]       = (bool)(sessionLogMask & SLOG_N1);
    sl["n2"]       = (bool)(sessionLogMask & SLOG_N2);
    sl["tot"]      = (bool)(sessionLogMask & SLOG_TOT);
    sl["oil_temp"] = (bool)(sessionLogMask & SLOG_OIL_TEMP);
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
    sl["wet_glow"]   = (bool)(sessionLogMask & SLOG_WET_GLOW);
    sl["glow_current"] = (bool)(sessionLogMask & SLOG_GLOW_CURRENT);
    sl["ign_current"]  = (bool)(sessionLogMask & SLOG_IGN_CURRENT);
    sl["ign2_current"] = (bool)(sessionLogMask & SLOG_IGN2_CURRENT);
    sl["oil_current"]  = (bool)(sessionLogMask & SLOG_OIL_CURRENT);
    sl["fp2"]        = (bool)(sessionLogMask & SLOG_FP2);
    sl["ab"]         = (bool)(sessionLogMask & SLOG_AB);
    sl["prop"]       = (bool)(sessionLogMask & SLOG_PROP);
    sl["oil_pct"]    = (bool)(sessionLogMask & SLOG_OIL_PCT);
    sl["loop"]       = (bool)(sessionLogMask & SLOG_LOOP);
    sl["interval_ms"]= sessionLogIntervalMs;

    auto stats = doc["stats"].to<JsonObject>();
    stats["total_run_seconds"] = totalRunSeconds;
    stats["start_attempt_count"] = startAttemptCount;
    stats["run_count"] = runCount;

    // ── Automation rules ──────────────────────────────────────────
    if (ruleCount > 0) {
        auto arr = doc["rules"].to<JsonArray>();
        for (int i = 0; i < ruleCount; i++) {
            const Rule& r = rules[i];
            auto jr = arr.add<JsonObject>();
            jr["enabled"]   = r.enabled;
            jr["kind"]      = r.kind;
            jr["op"]        = r.op;
            jr["threshold"] = r.threshold;
            jr["on_value"]  = r.onValue;
            jr["off_value"] = r.offValue;
            jr["hysteresis"]= r.hysteresis;
            jr["input_min"] = r.inputMin;
            jr["input_max"] = r.inputMax;
            jr["output_min"]= r.outputMin;
            jr["output_max"]= r.outputMax;
            jr["mode_mask"] = r.modeMask;
            jr["name"]      = r.name;
            jr["source"]    = r.sourceId;
            jr["target"]    = r.targetId;
        }
    }
}
