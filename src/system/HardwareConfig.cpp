#include "HardwareConfig.h"
#include "hardware_profile.h"
#include <LittleFS.h>
#include <cstring>

// ── Static member definitions ─────────────────────────────────
// Default values mirror hardware_profile.h so that a missing
// hardware.json produces identical behaviour to the current build.

char  HardwareConfig::profileId[64]    = {};
char  HardwareConfig::profileDesc[64]  = {};
char  HardwareConfig::wifiPassword[32] = {};   // empty = open network
bool  HardwareConfig::hasAfterburner   = false;
bool  HardwareConfig::hasTwoShaft      = false;

// Physical controls
int   HardwareConfig::stopPin          = OT_STOP_PIN;
bool  HardwareConfig::stopActiveH      = false;  // active-low: button connects pin to GND
int   HardwareConfig::startPin         = OT_START_PIN;
bool  HardwareConfig::startActiveH     = false;  // active-low

// Sensor feature flags
bool  HardwareConfig::hasN1Rpm         = true;
bool  HardwareConfig::hasN2Rpm         = false;
bool  HardwareConfig::hasTot           = true;
bool  HardwareConfig::hasTit           = false;
bool  HardwareConfig::hasOilPress      = true;
bool  HardwareConfig::hasFlame         = true;
bool  HardwareConfig::hasFuelFlow      = false;
bool  HardwareConfig::hasFuelPress     = false;
bool  HardwareConfig::hasP1            = false;
bool  HardwareConfig::hasP2            = false;
bool  HardwareConfig::hasThrottleInput = true;
bool  HardwareConfig::hasIdleInput     = true;
bool  HardwareConfig::hasOilTemp       = false;
bool  HardwareConfig::hasBattVoltage   = false;
bool  HardwareConfig::hasTorque        = false;

// Sensor pins & params
int   HardwareConfig::n1RpmPin         = OT_N1_RPM_PIN;
float HardwareConfig::n1RpmPpr         = OT_N1_RPM_PPR;
int   HardwareConfig::n2RpmPin         = 27;
float HardwareConfig::n2RpmPpr         = 0.633f;
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
int   HardwareConfig::fuelFlowPin      = 36;
int   HardwareConfig::fuelPressPin     = 36;
int   HardwareConfig::p1Pin            = 36;
int   HardwareConfig::p2Pin            = 39;
int   HardwareConfig::throttleInputPin = OT_THROTTLE_INPUT_PIN;
bool  HardwareConfig::throttleInputRcPwm = false;
int   HardwareConfig::idleInputPin     = OT_IDLE_INPUT_PIN;
bool  HardwareConfig::idleInputRcPwm   = false;

char  HardwareConfig::oilTempChip[12]  = "ntc";
int   HardwareConfig::oilTempPin       = -1;
int   HardwareConfig::oilTempCs        = -1;
int   HardwareConfig::oilTempMiso      = -1;
int   HardwareConfig::oilTempMosi      = -1;
char  HardwareConfig::oilTempTcType[4] = "K";
float HardwareConfig::ntcBeta          = 3950.0f;
float HardwareConfig::ntcR0            = 10000.0f;
float HardwareConfig::ntcRFixed        = 10000.0f;
int   HardwareConfig::battVoltPin      = -1;
float HardwareConfig::battVoltDivider  = 5.7f;
int   HardwareConfig::torquePin        = -1;
float HardwareConfig::torqueScale      = 30.3f;
float HardwareConfig::torqueOffset     = 0.0f;

// Actuator feature flags
bool  HardwareConfig::hasThrottle      = true;
bool  HardwareConfig::hasStarter       = true;
bool  HardwareConfig::hasOilPump       = true;
bool  HardwareConfig::hasFuelSol       = true;
bool  HardwareConfig::hasIgniter       = true;
bool  HardwareConfig::hasIgniter2      = false;
bool  HardwareConfig::hasStarterEn     = true;
bool  HardwareConfig::hasAbSol         = false;
bool  HardwareConfig::hasAirstarterSol = false;
bool  HardwareConfig::hasCoolFan       = false;
bool  HardwareConfig::hasAbPump        = false;
bool  HardwareConfig::hasOilScavengePump = false;
bool  HardwareConfig::hasFuelPump2     = false;
bool  HardwareConfig::hasBleedValve    = false;
bool  HardwareConfig::hasPropPitch     = false;
bool  HardwareConfig::hasGlowPlug      = false;
bool  HardwareConfig::hasGlowCurrentSensor    = false;
bool  HardwareConfig::hasIgniterCurrentSensor  = false;
bool  HardwareConfig::hasGovernor      = false;
bool  HardwareConfig::hasMAVLink       = false;
bool  HardwareConfig::hasStatusLed     = false;
bool  HardwareConfig::hasClusterSerial = false;
bool  HardwareConfig::hasBuzzer        = false;
int   HardwareConfig::buzzerPin        = -1;

int   HardwareConfig::fuelPump2Pin     = -1;
int   HardwareConfig::fuelPump2Type    = 1;   // ledc_pwm
int   HardwareConfig::fuelPump2MinUs   = 1000;
int   HardwareConfig::fuelPump2MaxUs   = 2000;
bool  HardwareConfig::fuelPump2ActiveH = true;
int   HardwareConfig::fuelPump2FreqHz  = 10000;
int   HardwareConfig::fuelPump2ResBits = 12;
int   HardwareConfig::bleedValveType    = 0;     // 0=on-off, 1=servo, 2=ledc_pwm
int   HardwareConfig::bleedValvePin    = -1;
bool  HardwareConfig::bleedValveActiveH = true;
int   HardwareConfig::bleedValveMinUs  = 1000;
int   HardwareConfig::bleedValveMaxUs  = 2000;
int   HardwareConfig::bleedValveFreqHz = 1000;
int   HardwareConfig::bleedValveResBits = 10;
int   HardwareConfig::propPitchType    = 0;     // 0=servo, 1=ledc_pwm, 2=on-off
int   HardwareConfig::propPitchPin     = -1;
int   HardwareConfig::propPitchMinUs   = 1000;
int   HardwareConfig::propPitchMaxUs   = 2000;
int   HardwareConfig::propPitchFreqHz  = 1000;
int   HardwareConfig::propPitchResBits = 10;
bool  HardwareConfig::propPitchActiveH = true;
int   HardwareConfig::glowPlugPin      = -1;
int   HardwareConfig::glowPlugFreqHz   = 1000;
int   HardwareConfig::glowPlugResBits  = 8;
int   HardwareConfig::glowCurrentPin           = -1;
float HardwareConfig::glowCurrentMvPerA        = 185.0f;
float HardwareConfig::glowCurrentZeroV         = 1.65f;
float HardwareConfig::glowCurrentReadyAmps     = 3.0f;
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
int   HardwareConfig::throttleLedcFreqHz  = 10000;
int   HardwareConfig::throttleLedcBits    = 12;

int   HardwareConfig::starterPin          = OT_STARTER_PIN;
int   HardwareConfig::starterType         = 0;     // default: servo
int   HardwareConfig::starterMinUs        = OT_STARTER_SERVO_MIN_US;
int   HardwareConfig::starterMaxUs        = OT_STARTER_SERVO_MAX_US;
bool  HardwareConfig::starterInverted     = false;
int   HardwareConfig::starterLedcFreqHz   = 10000;
int   HardwareConfig::starterLedcBits     = 12;
bool  HardwareConfig::starterAssistEnabled = true;   // enabled by default for servo/PWM types

int   HardwareConfig::oilPumpPin       = OT_OIL_PUMP_PIN;
#ifdef OT_OIL_PUMP_ONOFF
int   HardwareConfig::oilPumpType      = 2;   // on-off
bool  HardwareConfig::oilPumpActiveH   = OT_OIL_PUMP_ONOFF_ACTIVE_H;
int   HardwareConfig::oilPumpMinUs     = 1000;
int   HardwareConfig::oilPumpMaxUs     = 2000;
int   HardwareConfig::oilPumpFreqHz    = 10000;
int   HardwareConfig::oilPumpResBits   = 12;
#else
int   HardwareConfig::oilPumpType      = 1;   // ledc_pwm
bool  HardwareConfig::oilPumpActiveH   = true;
int   HardwareConfig::oilPumpMinUs     = 1000;
int   HardwareConfig::oilPumpMaxUs     = 2000;
int   HardwareConfig::oilPumpFreqHz    = OT_OIL_PUMP_FREQ_HZ;
int   HardwareConfig::oilPumpResBits   = OT_OIL_PUMP_RES_BITS;
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

int   HardwareConfig::abSolPin         = -1;
bool  HardwareConfig::abSolActiveH     = true;
int   HardwareConfig::airstarterSolPin = -1;
bool  HardwareConfig::airstarterSolActiveH = true;

int   HardwareConfig::coolFanPin       = -1;
int   HardwareConfig::coolFanType      = 2;   // on-off default
int   HardwareConfig::coolFanMinUs     = 1000;
int   HardwareConfig::coolFanMaxUs     = 2000;
bool  HardwareConfig::coolFanActiveH   = true;
int   HardwareConfig::coolFanFreqHz    = 10000;
int   HardwareConfig::coolFanResBits   = 12;

int   HardwareConfig::abPumpPin        = -1;
int   HardwareConfig::abPumpType       = 2;   // on-off default
int   HardwareConfig::abPumpMinUs      = 1000;
int   HardwareConfig::abPumpMaxUs      = 2000;
bool  HardwareConfig::abPumpActiveH    = true;
int   HardwareConfig::abPumpFreqHz     = 10000;
int   HardwareConfig::abPumpResBits    = 12;

int   HardwareConfig::oilScavPumpPin     = -1;
int   HardwareConfig::oilScavPumpType    = 2;
bool  HardwareConfig::oilScavPumpActiveH = true;
int   HardwareConfig::oilScavPumpFreqHz  = 10000;
int   HardwareConfig::oilScavPumpResBits = 12;

int   HardwareConfig::abTriggerSource    = 0;   // 0=manual
bool  HardwareConfig::abRequiresArmSwitch= false;
int   HardwareConfig::abArmSwitchPin     = -1;
bool  HardwareConfig::abArmSwitchActiveH = false;
int   HardwareConfig::abSwitchPin        = -1;
bool  HardwareConfig::abSwitchActiveH    = false;
int   HardwareConfig::abInputPin         = -1;
int   HardwareConfig::abInputThreshold   = 2048;

bool  HardwareConfig::hasAbFlame         = false;
int   HardwareConfig::abFlamePin         = -1;
int   HardwareConfig::abFlameThreshold   = 500;

int   HardwareConfig::statusLedPin     = -1;

// Cluster serial
int   HardwareConfig::clusterTxPin     = 17;
int   HardwareConfig::clusterBaud      = 115200;
int   HardwareConfig::clusterIntervalMs= 50;

// Controller feature flags
bool  HardwareConfig::hasOilLoop       = true;
bool  HardwareConfig::hasThrottleSlew  = true;
bool  HardwareConfig::hasDynamicIdle   = true;

// Safety enables
bool  HardwareConfig::safetyOverspeed  = true;
bool  HardwareConfig::safetyOvertemp   = true;
bool  HardwareConfig::safetyLowOil     = true;
bool  HardwareConfig::safetyOilZero    = true;
bool  HardwareConfig::safetyFlameout   = true;
bool  HardwareConfig::safetyLowFuel    = false;
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

// Sequences
char  HardwareConfig::startupSeq[MAX_SEQ_BLOCKS][24] = {
    "OilPrime", "StarterSpin", "PreIgnSpark", "FuelOpen",
    "FuelPumpIdle", "FlameConfirm", "TimedDelay", "Spool", "SafetyHold"
};
int   HardwareConfig::startupSeqLen    = 9;

char  HardwareConfig::shutdownSeq[MAX_SEQ_BLOCKS][24] = {
    "ImmediateCut", "RPMDrop", "CooldownSpin", "FinalStop"
};
int   HardwareConfig::shutdownSeqLen   = 4;

char  HardwareConfig::abSeq[MAX_SEQ_BLOCKS][24]    = {};
int   HardwareConfig::abSeqLen                     = 0;
char  HardwareConfig::abShutSeq[MAX_SEQ_BLOCKS][24]= {};
int   HardwareConfig::abShutSeqLen                 = 0;

// ── Load ──────────────────────────────────────────────────────
void HardwareConfig::load() {
    applyDefaults();

    // ── Migration: always check legacy file first, even if unified file exists.
    if (LittleFS.exists(LEGACY_PATH)) {
        File old = LittleFS.open(LEGACY_PATH, "r");
        if (old) {
            JsonDocument doc;
            if (deserializeJson(doc, old) == DeserializationError::Ok) {
                old.close();
                _fromDoc(doc);
                save();
                LittleFS.remove(LEGACY_PATH);
                Serial.println("[HWCfg] Migrated hardware.json -> ecu_config.json");
                return;
            }
            old.close();
        }
    }

    if (!LittleFS.exists(PATH)) {
        Serial.println("[HWCfg] No ecu_config.json — using compiled defaults, generating file");
        save();
        return;
    }

    File f = LittleFS.open(PATH, "r");
    if (!f) {
        Serial.println("[HWCfg] Failed to open ecu_config.json — using defaults");
        return;
    }
    JsonDocument fullDoc;
    DeserializationError err = deserializeJson(fullDoc, f);
    f.close();
    if (err) {
        Serial.printf("[HWCfg] JSON parse error: %s — using defaults\n", err.c_str());
        return;
    }

    // New unified format has a "hardware" key; legacy flat format does not
    JsonDocument workDoc;
    if (fullDoc[SECTION].is<JsonObject>()) {
        workDoc.set(fullDoc[SECTION]);
    } else {
        workDoc.set(fullDoc);   // legacy flat — re-save in new format next save()
    }

    _fromDoc(workDoc);
    Serial.printf("[HWCfg] Loaded OK — profile: %s\n", profileId);
}

// ── Save ──────────────────────────────────────────────────────
bool HardwareConfig::save() {
    // Read-modify-write: preserve other sections (settings etc.)
    JsonDocument fullDoc;
    File fr = LittleFS.open(PATH, "r");
    if (fr) { deserializeJson(fullDoc, fr); fr.close(); }

    JsonDocument hwDoc;
    _toDoc(hwDoc);
    fullDoc[SECTION].set(hwDoc);

    File fw = LittleFS.open(PATH, "w");
    if (!fw) {
        Serial.println("[HWCfg] Failed to open ecu_config.json for write");
        return false;
    }
    serializeJsonPretty(fullDoc, fw);
    fw.close();
    return true;
}

// ── Apply defaults ─────────────────────────────────────────────
// Called before load() to seed all values from hardware_profile.h.
// Static member initialisers already handle this at program start;
// this function is used when resetting to defaults at runtime.
void HardwareConfig::applyDefaults() {
    strncpy(profileId,   "OpenTurbine",   sizeof(profileId)   - 1);
    strncpy(profileDesc, OT_PROFILE_DESC, sizeof(profileDesc) - 1);
    hasAfterburner = false;
    hasTwoShaft    = false;

    stopPin      = OT_STOP_PIN;  stopActiveH  = false;
    startPin     = OT_START_PIN; startActiveH = false;

    hasN1Rpm  = true;  hasN2Rpm = false; hasTot = true; hasTit = false;
    hasOilPress = true; hasFlame = true;
    hasFuelFlow = false; hasFuelPress = false; hasP1 = false; hasP2 = false;
    hasThrottleInput = true; hasIdleInput = true;
    hasOilTemp = false; hasBattVoltage = false; hasTorque = false;

    n1RpmPin  = OT_N1_RPM_PIN; n1RpmPpr = OT_N1_RPM_PPR;
    n2RpmPin  = 27;  n2RpmPpr  = 0.633f;
    strncpy(totChip, "max6675", sizeof(totChip) - 1);
    strncpy(totTcType, "K", sizeof(totTcType) - 1);
    totClk    = OT_TOT_CLK; totCs = OT_TOT_CS; totMiso = OT_TOT_MISO; totMosi = -1;
    strncpy(titChip, "max6675", sizeof(titChip) - 1);
    strncpy(titTcType, "K", sizeof(titTcType) - 1);
    titClk = -1; titCs = -1; titMiso = -1; titMosi = -1;
    oilPressPin = OT_OIL_PRESS_PIN; flamePin = OT_FLAME_PIN;
    fuelFlowPin = 36; fuelPressPin = 36; p1Pin = 36; p2Pin = 39;
    throttleInputPin = OT_THROTTLE_INPUT_PIN; throttleInputRcPwm = false;
    idleInputPin     = OT_IDLE_INPUT_PIN;     idleInputRcPwm     = false;

    strncpy(oilTempChip, "ntc", sizeof(oilTempChip) - 1);
    oilTempPin = -1; oilTempCs = -1; oilTempMiso = -1; oilTempMosi = -1;
    strncpy(oilTempTcType, "K", sizeof(oilTempTcType) - 1);
    ntcBeta = 3950.0f; ntcR0 = 10000.0f; ntcRFixed = 10000.0f;
    battVoltPin = -1; battVoltDivider = 5.7f;
    torquePin = -1; torqueScale = 30.3f; torqueOffset = 0.0f;

    hasThrottle = true; hasStarter = true; hasOilPump = true;
    hasFuelSol  = true; hasIgniter = true; hasIgniter2 = false; hasStarterEn = true;
    hasAbSol = false; hasAirstarterSol = false; hasCoolFan = false;
    hasAbPump = false; hasFuelPump2 = false; hasBleedValve = false;
    hasPropPitch = false; hasGlowPlug = false;
    hasGlowCurrentSensor = false; hasIgniterCurrentSensor = false;
    hasGovernor = false; hasMAVLink = false;
    hasStatusLed = false; hasClusterSerial = false;
    hasBuzzer = false; buzzerPin = -1;

    fuelPump2Pin = -1; fuelPump2Type = 1; fuelPump2MinUs = 1000; fuelPump2MaxUs = 2000;
    fuelPump2ActiveH = true; fuelPump2FreqHz = 10000; fuelPump2ResBits = 12;
    bleedValveType = 0; bleedValvePin = -1; bleedValveActiveH = true;
    bleedValveMinUs = 1000; bleedValveMaxUs = 2000; bleedValveFreqHz = 1000; bleedValveResBits = 10;
    propPitchType = 0; propPitchPin = -1; propPitchMinUs = 1000; propPitchMaxUs = 2000;
    propPitchFreqHz = 1000; propPitchResBits = 10; propPitchActiveH = true;
    glowPlugPin = -1; glowPlugFreqHz = 1000; glowPlugResBits = 8;
    glowCurrentPin = -1; glowCurrentMvPerA = 185.0f; glowCurrentZeroV = 1.65f; glowCurrentReadyAmps = 3.0f;
    mavlinkTxPin = -1; mavlinkBaud = 57600; mavlinkIntervalMs = 100;

    throttlePin        = OT_THROTTLE_PIN;
    throttleType       = 0; throttleInverted = false;
    throttleMinUs      = OT_THROTTLE_SERVO_MIN_US;
    throttleMaxUs      = OT_THROTTLE_SERVO_MAX_US;
    throttleLedcFreqHz = 10000; throttleLedcBits = 12;

    starterPin        = OT_STARTER_PIN;
    starterType       = 0; starterInverted = false;
    starterMinUs      = OT_STARTER_SERVO_MIN_US;
    starterMaxUs      = OT_STARTER_SERVO_MAX_US;
    starterLedcFreqHz = 10000; starterLedcBits = 12;

    oilPumpPin     = OT_OIL_PUMP_PIN;
    oilPumpMinUs   = 1000; oilPumpMaxUs = 2000;
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

    igniter2Pin = -1; igniter2ActiveH = true; igniter2Pwm = false;
    igniter2DwellMs = 6; igniter2RestMs = 3;

    abSolPin = -1; abSolActiveH = true;
    airstarterSolPin = -1; airstarterSolActiveH = true;

    coolFanPin = -1; coolFanType = 2; coolFanMinUs = 1000; coolFanMaxUs = 2000;
    coolFanActiveH = true; coolFanFreqHz = 10000; coolFanResBits = 12;

    abPumpPin = -1; abPumpType = 2; abPumpMinUs = 1000; abPumpMaxUs = 2000;
    abPumpActiveH = true; abPumpFreqHz = 10000; abPumpResBits = 12;

    hasOilScavengePump = false;
    oilScavPumpPin     = -1;
    oilScavPumpType    = 2;
    oilScavPumpActiveH = true;
    oilScavPumpFreqHz  = 10000;
    oilScavPumpResBits = 12;

    abTriggerSource     = 0;
    abRequiresArmSwitch = false;
    abArmSwitchPin      = -1;
    abArmSwitchActiveH  = false;
    abSwitchPin         = -1;
    abSwitchActiveH     = false;
    abInputPin          = -1;
    abInputThreshold    = 2048;
    hasAbFlame          = false;
    abFlamePin          = -1;
    abFlameThreshold    = 500;

    statusLedPin = -1;

    clusterTxPin    = 17;
    clusterBaud     = 115200;
    clusterIntervalMs = 50;

    hasOilLoop      = true;
    hasThrottleSlew = true;
    hasDynamicIdle  = true;

    safetyOverspeed = true;
    safetyOvertemp  = true;
    safetyLowOil    = true;
    safetyOilZero   = true;
    safetyFlameout  = true;
    safetyLowFuel       = false;
    safetyHotStart      = false;
    safetyTitOvertemp   = false;
    safetyOilTempHigh   = false;
    safetyFuelPressLow  = false;
    safetyBattLow       = false;
    safetySurge         = false;

    const char* defStart[] = {
        "OilPrime","StarterSpin","PreIgnSpark","FuelOpen",
        "FuelPumpIdle","FlameConfirm","TimedDelay","Spool","SafetyHold"
    };
    startupSeqLen = 9;
    for (int i = 0; i < startupSeqLen; i++)
        strncpy(startupSeq[i], defStart[i], sizeof(startupSeq[i]) - 1);

    const char* defStop[] = { "ImmediateCut","RPMDrop","CooldownSpin","FinalStop" };
    shutdownSeqLen = 4;
    for (int i = 0; i < shutdownSeqLen; i++)
        strncpy(shutdownSeq[i], defStop[i], sizeof(shutdownSeq[i]) - 1);

    // AB ignition: check conditions → open solenoid → start pump → torch spike → confirm flame → stabilize
    const char* defAbIgn[] = {
        "ABCheckReady","ABSolOpen","ABPumpOn","ABIgnite","ABFlameConfirm","ABStabilize"
    };
    abSeqLen = 6;
    memset(abSeq, 0, sizeof(abSeq));
    for (int i = 0; i < abSeqLen; i++)
        strncpy(abSeq[i], defAbIgn[i], sizeof(abSeq[i]) - 1);

    // AB shutdown: close solenoid first, then cut pump
    const char* defAbShut[] = { "ABSolClose", "ABPumpOff" };
    abShutSeqLen = 2;
    memset(abShutSeq, 0, sizeof(abShutSeq));
    for (int i = 0; i < abShutSeqLen; i++)
        strncpy(abShutSeq[i], defAbShut[i], sizeof(abShutSeq[i]) - 1);
}

// ── toJson ────────────────────────────────────────────────────
size_t HardwareConfig::toJson(char* buf, size_t len) {
    JsonDocument doc;
    _toDoc(doc);
    return serializeJson(doc, buf, len);
}

// ── fromJson ──────────────────────────────────────────────────
bool HardwareConfig::fromJson(const char* json, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return false;
    _fromDoc(doc);
    return true;
}

// ── _toDoc ────────────────────────────────────────────────────
void HardwareConfig::_toDoc(JsonDocument& doc) {
    doc["profile_id"]       = profileId;
    doc["profile_desc"]     = profileDesc;
    doc["wifi_password"]    = wifiPassword;
    doc["has_afterburner"]  = hasAfterburner;
    doc["has_two_shaft"]    = hasTwoShaft;

    auto ctrl = doc["controls"].to<JsonObject>();
    ctrl["stop_pin"]      = stopPin;
    ctrl["stop_active_h"] = stopActiveH;
    ctrl["start_pin"]     = startPin;
    ctrl["start_active_h"]= startActiveH;

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
    oilt["pin"] = oilTempPin; oilt["cs"] = oilTempCs;
    oilt["miso"] = oilTempMiso; oilt["mosi"] = oilTempMosi;
    oilt["tc_type"] = oilTempTcType;
    oilt["ntc_beta"]    = ntcBeta;
    oilt["ntc_r0"]      = ntcR0;
    oilt["ntc_r_fixed"] = ntcRFixed;

    auto bvs = sensors["batt_voltage"].to<JsonObject>();
    bvs["enabled"] = hasBattVoltage; bvs["pin"] = battVoltPin;
    bvs["divider"] = battVoltDivider;

    auto torqs = sensors["torque"].to<JsonObject>();
    torqs["enabled"] = hasTorque; torqs["pin"] = torquePin;
    torqs["scale"] = torqueScale; torqs["offset"] = torqueOffset;

    auto acts = doc["actuators"].to<JsonObject>();

    auto thr = acts["throttle"].to<JsonObject>();
    thr["enabled"]   = hasThrottle; thr["pin"] = throttlePin;
    thr["type"]      = throttleType;
    thr["min_us"]    = throttleMinUs; thr["max_us"] = throttleMaxUs;
    thr["inverted"]  = throttleInverted;
    thr["ledc_freq"] = throttleLedcFreqHz; thr["ledc_bits"] = throttleLedcBits;

    auto str = acts["starter"].to<JsonObject>();
    str["enabled"]   = hasStarter; str["pin"] = starterPin;
    str["type"]      = starterType;
    str["min_us"]    = starterMinUs; str["max_us"] = starterMaxUs;
    str["inverted"]         = starterInverted;
    str["ledc_freq"]        = starterLedcFreqHz; str["ledc_bits"] = starterLedcBits;
    str["assist_enabled"]   = starterAssistEnabled;

    auto oilp = acts["oil_pump"].to<JsonObject>();
    oilp["enabled"] = hasOilPump; oilp["pin"] = oilPumpPin;
    oilp["type"] = oilPumpType; oilp["active_h"] = oilPumpActiveH;
    oilp["min_us"] = oilPumpMinUs; oilp["max_us"] = oilPumpMaxUs;
    oilp["freq_hz"] = oilPumpFreqHz; oilp["res_bits"] = oilPumpResBits;

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

    auto abp = acts["ab_pump"].to<JsonObject>();
    abp["enabled"] = hasAbPump; abp["pin"] = abPumpPin;
    abp["type"] = abPumpType; abp["active_h"] = abPumpActiveH;
    abp["min_us"] = abPumpMinUs; abp["max_us"] = abPumpMaxUs;
    abp["freq_hz"] = abPumpFreqHz; abp["res_bits"] = abPumpResBits;

    auto scav = acts["oil_scavenge_pump"].to<JsonObject>();
    scav["enabled"]   = hasOilScavengePump;
    scav["pin"]       = oilScavPumpPin;
    scav["type"]      = oilScavPumpType;
    scav["active_h"]  = oilScavPumpActiveH;
    scav["freq_hz"]   = oilScavPumpFreqHz;
    scav["res_bits"]  = oilScavPumpResBits;

    auto fp2 = acts["fuel_pump2"].to<JsonObject>();
    fp2["enabled"]  = hasFuelPump2; fp2["pin"] = fuelPump2Pin;
    fp2["type"]     = fuelPump2Type; fp2["active_h"] = fuelPump2ActiveH;
    fp2["min_us"]   = fuelPump2MinUs; fp2["max_us"] = fuelPump2MaxUs;
    fp2["freq_hz"]  = fuelPump2FreqHz; fp2["res_bits"] = fuelPump2ResBits;

    auto blv = acts["bleed_valve"].to<JsonObject>();
    blv["enabled"] = hasBleedValve; blv["pin"] = bleedValvePin;
    blv["type"] = bleedValveType; blv["active_h"] = bleedValveActiveH;
    blv["min_us"] = bleedValveMinUs; blv["max_us"] = bleedValveMaxUs;
    blv["freq_hz"] = bleedValveFreqHz; blv["res_bits"] = bleedValveResBits;

    auto pps = acts["prop_pitch"].to<JsonObject>();
    pps["enabled"] = hasPropPitch; pps["pin"] = propPitchPin;
    pps["type"] = propPitchType;
    pps["min_us"] = propPitchMinUs; pps["max_us"] = propPitchMaxUs;
    pps["freq_hz"] = propPitchFreqHz; pps["res_bits"] = propPitchResBits;
    pps["active_h"] = propPitchActiveH;

    auto glw = acts["glow_plug"].to<JsonObject>();
    glw["enabled"] = hasGlowPlug; glw["pin"] = glowPlugPin;
    glw["freq_hz"] = glowPlugFreqHz; glw["res_bits"] = glowPlugResBits;
    glw["current_pin"]    = glowCurrentPin;
    glw["current_mv_a"]   = glowCurrentMvPerA;
    glw["current_zero_v"] = glowCurrentZeroV;
    glw["current_ready_a"]= glowCurrentReadyAmps;
    glw["has_current"]    = hasGlowCurrentSensor;

    auto led = acts["status_led"].to<JsonObject>();
    led["enabled"] = hasStatusLed; led["pin"] = statusLedPin;

    auto clus = doc["cluster_serial"].to<JsonObject>();
    clus["enabled"] = hasClusterSerial; clus["tx_pin"] = clusterTxPin;
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

    auto saf = doc["safety"].to<JsonObject>();
    saf["overspeed"]  = safetyOverspeed;
    saf["overtemp"]   = safetyOvertemp;
    saf["low_oil"]    = safetyLowOil;
    saf["oil_zero"]   = safetyOilZero;
    saf["flameout"]   = safetyFlameout;
    saf["low_fuel"]       = safetyLowFuel;
    saf["hot_start"]      = safetyHotStart;
    saf["tit_overtemp"]   = safetyTitOvertemp;
    saf["oil_temp_high"]  = safetyOilTempHigh;
    saf["fuel_press_low"] = safetyFuelPressLow;
    saf["batt_low"]       = safetyBattLow;
    saf["surge"]          = safetySurge;

    auto ss = doc["startup_seq"].to<JsonArray>();
    for (int i = 0; i < startupSeqLen; i++) ss.add(startupSeq[i]);

    auto ds = doc["shutdown_seq"].to<JsonArray>();
    for (int i = 0; i < shutdownSeqLen; i++) ds.add(shutdownSeq[i]);

    auto abt = doc["ab_trigger"].to<JsonObject>();
    abt["source"]           = abTriggerSource;
    abt["requires_arm"]     = abRequiresArmSwitch;
    abt["arm_pin"]          = abArmSwitchPin;
    abt["arm_active_h"]     = abArmSwitchActiveH;
    abt["switch_pin"]       = abSwitchPin;
    abt["switch_active_h"]  = abSwitchActiveH;
    abt["input_pin"]        = abInputPin;
    abt["input_threshold"]  = abInputThreshold;

    auto abfl = doc["ab_flame"].to<JsonObject>();
    abfl["enabled"]   = hasAbFlame;
    abfl["pin"]       = abFlamePin;
    abfl["threshold"] = abFlameThreshold;

    auto as = doc["ab_seq"].to<JsonArray>();
    for (int i = 0; i < abSeqLen; i++) as.add(abSeq[i]);

    auto ass = doc["ab_shut_seq"].to<JsonArray>();
    for (int i = 0; i < abShutSeqLen; i++) ass.add(abShutSeq[i]);

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
        ch["active_modes"] = diCh[i].activeModes;
    }
}

// ── _fromDoc ─────────────────────────────────────────────────
void HardwareConfig::_fromDoc(const JsonDocument& doc) {
    const char* id   = doc["profile_id"]    | profileId;
    const char* desc = doc["profile_desc"]  | profileDesc;
    const char* pwd  = doc["wifi_password"] | (const char*)wifiPassword;
    strncpy(profileId,    id,   sizeof(profileId)    - 1);
    strncpy(profileDesc,  desc, sizeof(profileDesc)  - 1);
    strncpy(wifiPassword, pwd,  sizeof(wifiPassword) - 1);
    if (!doc["has_afterburner"].isNull()) hasAfterburner = doc["has_afterburner"].as<bool>();
    if (!doc["has_two_shaft"].isNull())   hasTwoShaft    = doc["has_two_shaft"].as<bool>();

    auto ctrl = doc["controls"];
    stopPin  = ctrl["stop_pin"]  | stopPin;
    if (!ctrl["stop_active_h"].isNull())  stopActiveH  = ctrl["stop_active_h"].as<bool>();
    startPin = ctrl["start_pin"] | startPin;
    if (!ctrl["start_active_h"].isNull()) startActiveH = ctrl["start_active_h"].as<bool>();

    auto s = doc["sensors"];

    auto n1 = s["n1_rpm"];
    if (!n1["enabled"].isNull()) hasN1Rpm = n1["enabled"].as<bool>();
    n1RpmPin = n1["pin"] | n1RpmPin;
    n1RpmPpr = n1["ppr"] | n1RpmPpr;

    auto n2 = s["n2_rpm"];
    if (!n2["enabled"].isNull()) hasN2Rpm = n2["enabled"].as<bool>();
    n2RpmPin = n2["pin"] | n2RpmPin;
    n2RpmPpr = n2["ppr"] | n2RpmPpr;

    auto tot = s["tot"];
    if (!tot["enabled"].isNull()) hasTot = tot["enabled"].as<bool>();
    { const char* v = tot["chip"]    | totChip;   strncpy(totChip,   v, sizeof(totChip)   - 1); }
    { const char* v = tot["tc_type"] | totTcType; strncpy(totTcType, v, sizeof(totTcType) - 1); }
    totClk  = tot["clk"]  | totClk;
    totCs   = tot["cs"]   | totCs;
    totMiso = tot["miso"] | totMiso;
    totMosi = tot["mosi"] | totMosi;

    auto tit = s["tit"];
    if (!tit["enabled"].isNull()) hasTit = tit["enabled"].as<bool>();
    { const char* v = tit["chip"]    | titChip;   strncpy(titChip,   v, sizeof(titChip)   - 1); }
    { const char* v = tit["tc_type"] | titTcType; strncpy(titTcType, v, sizeof(titTcType) - 1); }
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
    fuelFlowPin = ff["pin"] | fuelFlowPin;

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
    { const char* v = oilt["chip"] | oilTempChip; strncpy(oilTempChip, v, sizeof(oilTempChip) - 1); }
    oilTempPin  = oilt["pin"]  | oilTempPin;
    oilTempCs   = oilt["cs"]   | oilTempCs;
    oilTempMiso = oilt["miso"] | oilTempMiso;
    oilTempMosi = oilt["mosi"] | oilTempMosi;
    { const char* v = oilt["tc_type"] | oilTempTcType; strncpy(oilTempTcType, v, sizeof(oilTempTcType) - 1); }
    ntcBeta   = oilt["ntc_beta"]    | ntcBeta;
    ntcR0     = oilt["ntc_r0"]      | ntcR0;
    ntcRFixed = oilt["ntc_r_fixed"] | ntcRFixed;

    auto bvs = s["batt_voltage"];
    if (!bvs["enabled"].isNull()) hasBattVoltage = bvs["enabled"].as<bool>();
    battVoltPin     = bvs["pin"]     | battVoltPin;
    battVoltDivider = bvs["divider"] | battVoltDivider;

    auto torqs = s["torque"];
    if (!torqs["enabled"].isNull()) hasTorque = torqs["enabled"].as<bool>();
    torquePin    = torqs["pin"]    | torquePin;
    torqueScale  = torqs["scale"]  | torqueScale;
    torqueOffset = torqs["offset"] | torqueOffset;

    auto a = doc["actuators"];

    auto thr = a["throttle"];
    if (!thr["enabled"].isNull())  hasThrottle      = thr["enabled"].as<bool>();
    throttlePin        = thr["pin"]       | throttlePin;
    throttleType       = thr["type"]      | throttleType;
    throttleMinUs      = thr["min_us"]    | throttleMinUs;
    throttleMaxUs      = thr["max_us"]    | throttleMaxUs;
    if (!thr["inverted"].isNull()) throttleInverted  = thr["inverted"].as<bool>();
    throttleLedcFreqHz = thr["ledc_freq"] | throttleLedcFreqHz;
    throttleLedcBits   = thr["ledc_bits"] | throttleLedcBits;

    auto str = a["starter"];
    if (!str["enabled"].isNull())  hasStarter        = str["enabled"].as<bool>();
    starterPin         = str["pin"]       | starterPin;
    starterType        = str["type"]      | starterType;
    if (!str["inverted"].isNull()) starterInverted    = str["inverted"].as<bool>();
    starterLedcFreqHz  = str["ledc_freq"] | starterLedcFreqHz;
    starterLedcBits    = str["ledc_bits"] | starterLedcBits;
    starterMinUs = str["min_us"] | starterMinUs;
    starterMaxUs = str["max_us"] | starterMaxUs;
    if (!str["assist_enabled"].isNull()) starterAssistEnabled = str["assist_enabled"].as<bool>();

    auto op = a["oil_pump"];
    if (!op["enabled"].isNull())  hasOilPump  = op["enabled"].as<bool>();
    oilPumpPin     = op["pin"]      | oilPumpPin;
    oilPumpType    = op["type"]     | oilPumpType;
    if (!op["active_h"].isNull()) oilPumpActiveH = op["active_h"].as<bool>();
    // backward compat: old "onoff" bool → type 2
    if (!op["onoff"].isNull() && op["onoff"].as<bool>()) oilPumpType = 2;
    oilPumpMinUs   = op["min_us"]   | oilPumpMinUs;
    oilPumpMaxUs   = op["max_us"]   | oilPumpMaxUs;
    oilPumpFreqHz  = op["freq_hz"]  | oilPumpFreqHz;
    oilPumpResBits = op["res_bits"] | oilPumpResBits;

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
    if (!ign["has_current"].isNull()) hasIgniterCurrentSensor = ign["has_current"].as<bool>();

    auto ign2 = a["igniter2"];
    if (!ign2["enabled"].isNull()) hasIgniter2    = ign2["enabled"].as<bool>();
    igniter2Pin    = ign2["pin"]      | igniter2Pin;
    if (!ign2["active_h"].isNull()) igniter2ActiveH = ign2["active_h"].as<bool>();
    if (!ign2["pwm"].isNull())      igniter2Pwm     = ign2["pwm"].as<bool>();
    igniter2DwellMs = ign2["dwell_ms"] | igniter2DwellMs;
    igniter2RestMs  = ign2["rest_ms"]  | igniter2RestMs;

    auto sen = a["starter_en"];
    if (!sen["enabled"].isNull()) hasStarterEn   = sen["enabled"].as<bool>();
    starterEnPin       = sen["pin"]       | starterEnPin;
    if (!sen["active_h"].isNull()) starterEnActiveH = sen["active_h"].as<bool>();
    starterEnDelayMs   = sen["delay_ms"]  | starterEnDelayMs;

    auto abs2 = a["ab_sol"];
    if (!abs2["enabled"].isNull()) hasAbSol   = abs2["enabled"].as<bool>();
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

    auto abp = a["ab_pump"];
    if (!abp["enabled"].isNull()) hasAbPump = abp["enabled"].as<bool>();
    abPumpPin    = abp["pin"]      | abPumpPin;
    abPumpType   = abp["type"]     | abPumpType;
    if (!abp["active_h"].isNull()) abPumpActiveH = abp["active_h"].as<bool>();
    abPumpMinUs  = abp["min_us"]   | abPumpMinUs;
    abPumpMaxUs  = abp["max_us"]   | abPumpMaxUs;
    abPumpFreqHz = abp["freq_hz"]  | abPumpFreqHz;
    abPumpResBits= abp["res_bits"] | abPumpResBits;

    auto scav = a["oil_scavenge_pump"];
    if (!scav["enabled"].isNull()) hasOilScavengePump = scav["enabled"].as<bool>();
    oilScavPumpPin     = scav["pin"]      | oilScavPumpPin;
    oilScavPumpType    = scav["type"]     | oilScavPumpType;
    if (!scav["active_h"].isNull()) oilScavPumpActiveH = scav["active_h"].as<bool>();
    oilScavPumpFreqHz  = scav["freq_hz"]  | oilScavPumpFreqHz;
    oilScavPumpResBits = scav["res_bits"] | oilScavPumpResBits;

    auto fp2 = a["fuel_pump2"];
    if (!fp2["enabled"].isNull()) hasFuelPump2 = fp2["enabled"].as<bool>();
    fuelPump2Pin     = fp2["pin"]      | fuelPump2Pin;
    fuelPump2Type    = fp2["type"]     | fuelPump2Type;
    if (!fp2["active_h"].isNull()) fuelPump2ActiveH = fp2["active_h"].as<bool>();
    fuelPump2MinUs   = fp2["min_us"]   | fuelPump2MinUs;
    fuelPump2MaxUs   = fp2["max_us"]   | fuelPump2MaxUs;
    fuelPump2FreqHz  = fp2["freq_hz"]  | fuelPump2FreqHz;
    fuelPump2ResBits = fp2["res_bits"] | fuelPump2ResBits;

    auto blv = a["bleed_valve"];
    if (!blv["enabled"].isNull()) hasBleedValve  = blv["enabled"].as<bool>();
    bleedValveType   = blv["type"]     | bleedValveType;
    bleedValvePin    = blv["pin"]      | bleedValvePin;
    if (!blv["active_h"].isNull()) bleedValveActiveH = blv["active_h"].as<bool>();
    bleedValveMinUs  = blv["min_us"]   | bleedValveMinUs;
    bleedValveMaxUs  = blv["max_us"]   | bleedValveMaxUs;
    bleedValveFreqHz = blv["freq_hz"]  | bleedValveFreqHz;
    bleedValveResBits= blv["res_bits"] | bleedValveResBits;

    auto pps = a["prop_pitch"];
    if (!pps["enabled"].isNull()) hasPropPitch = pps["enabled"].as<bool>();
    propPitchType   = pps["type"]     | propPitchType;
    propPitchPin    = pps["pin"]      | propPitchPin;
    propPitchMinUs  = pps["min_us"]   | propPitchMinUs;
    propPitchMaxUs  = pps["max_us"]   | propPitchMaxUs;
    propPitchFreqHz = pps["freq_hz"]  | propPitchFreqHz;
    propPitchResBits= pps["res_bits"] | propPitchResBits;
    if (!pps["active_h"].isNull()) propPitchActiveH = pps["active_h"].as<bool>();

    auto glw = a["glow_plug"];
    if (!glw["enabled"].isNull()) hasGlowPlug  = glw["enabled"].as<bool>();
    glowPlugPin     = glw["pin"]      | glowPlugPin;
    glowPlugFreqHz  = glw["freq_hz"]  | glowPlugFreqHz;
    glowPlugResBits = glw["res_bits"] | glowPlugResBits;
    glowCurrentPin      = glw["current_pin"]     | glowCurrentPin;
    glowCurrentMvPerA   = glw["current_mv_a"]    | glowCurrentMvPerA;
    glowCurrentZeroV    = glw["current_zero_v"]  | glowCurrentZeroV;
    glowCurrentReadyAmps= glw["current_ready_a"] | glowCurrentReadyAmps;
    if (!glw["has_current"].isNull()) hasGlowCurrentSensor = glw["has_current"].as<bool>();

    auto led = a["status_led"];
    if (!led["enabled"].isNull()) hasStatusLed = led["enabled"].as<bool>();
    statusLedPin = led["pin"] | statusLedPin;

    auto clus = doc["cluster_serial"];
    if (!clus["enabled"].isNull()) hasClusterSerial = clus["enabled"].as<bool>();
    clusterTxPin     = clus["tx_pin"]     | clusterTxPin;
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

    auto contrl = doc["controllers"];
    if (!contrl["oil_loop"].isNull())      hasOilLoop      = contrl["oil_loop"].as<bool>();
    if (!contrl["throttle_slew"].isNull()) hasThrottleSlew = contrl["throttle_slew"].as<bool>();
    if (!contrl["dynamic_idle"].isNull())  hasDynamicIdle  = contrl["dynamic_idle"].as<bool>();
    if (!contrl["governor"].isNull())      hasGovernor     = contrl["governor"].as<bool>();

    auto saf = doc["safety"];
    if (!saf["overspeed"].isNull()) safetyOverspeed = saf["overspeed"].as<bool>();
    if (!saf["overtemp"].isNull())  safetyOvertemp  = saf["overtemp"].as<bool>();
    if (!saf["low_oil"].isNull())   safetyLowOil    = saf["low_oil"].as<bool>();
    if (!saf["oil_zero"].isNull())  safetyOilZero   = saf["oil_zero"].as<bool>();
    if (!saf["flameout"].isNull())   safetyFlameout  = saf["flameout"].as<bool>();
    if (!saf["low_fuel"].isNull())       safetyLowFuel       = saf["low_fuel"].as<bool>();
    if (!saf["hot_start"].isNull())      safetyHotStart      = saf["hot_start"].as<bool>();
    if (!saf["tit_overtemp"].isNull())   safetyTitOvertemp   = saf["tit_overtemp"].as<bool>();
    if (!saf["oil_temp_high"].isNull())  safetyOilTempHigh   = saf["oil_temp_high"].as<bool>();
    if (!saf["fuel_press_low"].isNull()) safetyFuelPressLow  = saf["fuel_press_low"].as<bool>();
    if (!saf["batt_low"].isNull())       safetyBattLow       = saf["batt_low"].as<bool>();
    if (!saf["surge"].isNull())          safetySurge         = saf["surge"].as<bool>();

    if (doc["startup_seq"].is<JsonArrayConst>()) {
        JsonArrayConst ss = doc["startup_seq"];
        int n = (int)ss.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        startupSeqLen = n;
        for (int i = 0; i < n; i++)
            strncpy(startupSeq[i], ss[i] | "", sizeof(startupSeq[i]) - 1);
    }

    if (doc["shutdown_seq"].is<JsonArrayConst>()) {
        JsonArrayConst ds = doc["shutdown_seq"];
        int n = (int)ds.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        shutdownSeqLen = n;
        for (int i = 0; i < n; i++)
            strncpy(shutdownSeq[i], ds[i] | "", sizeof(shutdownSeq[i]) - 1);
    }

    auto abt = doc["ab_trigger"];
    abTriggerSource    = abt["source"]          | abTriggerSource;
    if (!abt["requires_arm"].isNull())  abRequiresArmSwitch = abt["requires_arm"].as<bool>();
    abArmSwitchPin     = abt["arm_pin"]         | abArmSwitchPin;
    if (!abt["arm_active_h"].isNull())  abArmSwitchActiveH  = abt["arm_active_h"].as<bool>();
    abSwitchPin        = abt["switch_pin"]      | abSwitchPin;
    if (!abt["switch_active_h"].isNull()) abSwitchActiveH   = abt["switch_active_h"].as<bool>();
    abInputPin         = abt["input_pin"]       | abInputPin;
    abInputThreshold   = abt["input_threshold"] | abInputThreshold;

    auto abfl = doc["ab_flame"];
    if (!abfl["enabled"].isNull()) hasAbFlame   = abfl["enabled"].as<bool>();
    abFlamePin         = abfl["pin"]       | abFlamePin;
    abFlameThreshold   = abfl["threshold"] | abFlameThreshold;

    if (doc["ab_seq"].is<JsonArrayConst>()) {
        JsonArrayConst as = doc["ab_seq"];
        int n = (int)as.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        abSeqLen = n;
        for (int i = 0; i < n; i++)
            strncpy(abSeq[i], as[i] | "", sizeof(abSeq[i]) - 1);
    }

    if (doc["ab_shut_seq"].is<JsonArrayConst>()) {
        JsonArrayConst ass = doc["ab_shut_seq"];
        int n = (int)ass.size();
        if (n > MAX_SEQ_BLOCKS) n = MAX_SEQ_BLOCKS;
        abShutSeqLen = n;
        for (int i = 0; i < n; i++)
            strncpy(abShutSeq[i], ass[i] | "", sizeof(abShutSeq[i]) - 1);
    }

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
            diCh[i].activeModes = ch["active_modes"] | (uint8_t)0xFF;
            i++;
        }
    }
}
