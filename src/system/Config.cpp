#include "Config.h"
#include "hardware_profile.h"
#include <LittleFS.h>

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
float Config::startRpmThreshold       = 1000;
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
float Config::totRiseRateLimitDegPerSec  = 0.0f;
float Config::titLimit                   = 0.0f;
float Config::oilTempLimit               = 120.0f;
float Config::fuelPressMin               = 0.0f;
float Config::battVoltMin                = 0.0f;
float Config::surgeDetectRpmVariance     = 0.0f;

bool     Config::relightEnabled      = false;
float    Config::relightMinRpm       = 30000.0f;
int      Config::relightMaxAttempts  = 0;

uint32_t Config::toolFuelPrimeMs    = 3000;
uint32_t Config::toolOilPrimeMs     = 5000;
uint32_t Config::toolIgnTestMs      = 2000;
uint32_t Config::toolStartTestMs    = 2000;
uint32_t Config::toolFuelSolTestMs  = 1000;

uint32_t Config::wsIntervalMs       = 200;
uint32_t Config::snapshotIntervalMs = 5000;

float    Config::starterAssistPct    = 15.0f;
float    Config::starterAssistExitRpm = 1000.0f;

float    Config::starterRampPctPerSec = 10.0f;
float    Config::starterDemand        = 60.0f;  // %

float    Config::oilZeroBar          = 0.1f;
float    Config::oilPressureDeadband = 0.2f;

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

int      Config::cooldownSkipHoldMs  = 1000;

float    Config::fuelPumpIdleMinPct  = 8.0f;
float    Config::fuelPumpIdleMaxPct  = 18.0f;

int      Config::timedDelayMs            = 1000;
float    Config::modifiedIdleMultiplier  = 1.0f;

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
bool     Config::abPumpFollowThrottle       = false;
float    Config::abMainFuelOffsetPct        = 0.0f;
int      Config::abStabilizeMs              = 1000;
float    Config::abStabilizeMaxTot          = 0.0f;      // 0 = disabled

float    Config::rpmJumpThreshold    = 0.40f;
int      Config::rpmZeroStuckTicks   = 5;

float    Config::n1WarnRpm          = 90000.0f;   // default = rpmLimit * 0.9
float    Config::n2WarnRpm          = 22000.0f;
float    Config::totWarnC           = 0.0f;       // 0 = auto (totLimit - totSafeMargin)
float    Config::oilWarnBar         = 0.0f;       // 0 = auto (oilRunningMin)
bool     Config::clusterEnabled     = false;

bool     Config::pressureSensorsEnabled = false;

int      Config::rcMinUs            = 1000;
int      Config::rcMaxUs            = 2000;
int      Config::rcFailsafeMs       = 500;

uint32_t Config::sessionLogMask       = Config::SLOG_DEFAULT;
uint32_t Config::sessionLogIntervalMs = 500;   // 2 Hz default
float Config::governorTargetRpm     = 0.0f;
float Config::governorBandRpm       = 500.0f;
float Config::governorKp            = 0.001f;
float Config::governorPitchKp       = 0.0005f;
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

uint32_t Config::totalRunSeconds    = 0;

int   Config::throttleMinRaw        = 950;
int   Config::throttleMaxRaw        = 3150;
int   Config::flameThreshold        = 500;
float Config::oilPolyA              = 0;
float Config::oilPolyB              = 0;
float Config::oilPolyC              = 0;
float Config::oilPolyD              = 0;
float Config::oilPolyXMin           = 0;
float Config::oilPolyXMax           = 4095;
float Config::p1ZeroBar             = 0.0f;
float Config::p2ZeroBar             = 0.0f;
int   Config::fuelPressRawMin       = 0;
int   Config::fuelPressRawMax       = 4095;
float Config::fuelPressValMax       = 10.0f;

char  Config::profileId[64]         = {};
bool  Config::profileMatch          = false;

// ── Load ──────────────────────────────────────────────────────
void Config::load() {
    _applyDefaults();

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
                profileMatch = (strcmp(profileId, OT_PROFILE_ID) == 0);
                if (!profileMatch) {
                    // Profile mismatch in legacy file — warn but continue with defaults
                    // (do NOT fault: that bricks the device with no recovery path)
                    Serial.printf("[Config] WARNING: profile mismatch in legacy (fw=%s file=%s)"
                                  " — using defaults\n", OT_PROFILE_ID, profileId);
                    save();                       // write defaults into unified file
                    LittleFS.remove(LEGACY_PATH); // discard the mismatched legacy file
                    strncpy(profileId, OT_PROFILE_ID, sizeof(profileId) - 1);
                    profileMatch = true;
                    return;
                }
                _fromDoc(doc);
                save();
                LittleFS.remove(LEGACY_PATH);
                Serial.println("[Config] Migrated config.json -> ecu_config.json");
                return;
            }
            old.close();
        }
    }

    if (!LittleFS.exists(PATH)) {
        Serial.println("[Config] No ecu_config.json — generating defaults");
        save();
        strncpy(profileId, OT_PROFILE_ID, sizeof(profileId) - 1);
        profileMatch = true;
        return;
    }

    File f = LittleFS.open(PATH, "r");
    if (!f) {
        Serial.println("[Config] Failed to open ecu_config.json");
        profileMatch = false;
        return;
    }
    JsonDocument fullDoc;
    DeserializationError err = deserializeJson(fullDoc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
        profileMatch = false;
        return;
    }

    // New unified format has a "settings" key; legacy flat format does not
    JsonDocument workDoc;
    if (fullDoc[SECTION].is<JsonObject>()) {
        workDoc.set(fullDoc[SECTION]);
    } else {
        workDoc.set(fullDoc);   // legacy flat — re-save in new format next save()
    }

    const char* id = workDoc["profile_id"] | "";
    strncpy(profileId, id, sizeof(profileId) - 1);
    profileMatch = (strcmp(profileId, OT_PROFILE_ID) == 0);
    if (!profileMatch) {
        // Profile mismatch — warn and fall back to defaults; do NOT fault
        Serial.printf("[Config] WARNING: profile mismatch (fw=%s file=%s)"
                      " — using defaults, device stays in STANDBY\n",
                      OT_PROFILE_ID, profileId);
        _applyDefaults();
        strncpy(profileId, OT_PROFILE_ID, sizeof(profileId) - 1);
        profileMatch = true;
        save();
        return;
    }

    uint8_t ver = workDoc["config_version"] | 0;
    if (ver != CONFIG_VERSION) {
        Serial.printf("[Config] Version mismatch (file=%u expected=%u) — new fields use defaults\n",
                      ver, CONFIG_VERSION);
        // Signal the web UI to show a calibration reminder banner
        EngineData::instance().configVersionMismatch = true;
    }

    _fromDoc(workDoc);
    Serial.printf("[Config] Loaded OK — profile: %s\n", profileId);
}

void Config::save() {
    // Read-modify-write: preserve other sections (hardware etc.)
    JsonDocument fullDoc;
    File fr = LittleFS.open(PATH, "r");
    if (fr) { deserializeJson(fullDoc, fr); fr.close(); }

    JsonDocument settingsDoc;
    _toDoc(settingsDoc);
    fullDoc[SECTION].set(settingsDoc);

    File fw = LittleFS.open(PATH, "w");
    if (!fw) { Serial.println("[Config] Failed to open ecu_config.json for write"); return; }
    serializeJsonPretty(fullDoc, fw);
    fw.close();
}

bool Config::isLocked() {
    auto& ed = EngineData::instance();
    // Runtime dev mode (set at boot via OT_DEV_MODE, or toggled via web UI) unlocks everything
    if (ed.devMode) return false;
    auto m = ed.mode;
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

bool Config::fromJson(const char* json, size_t len) {
    if (isLocked()) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return false;

    const char* id = doc["profile_id"] | "";
    if (strcmp(id, OT_PROFILE_ID) != 0) return false;  // reject wrong profile

    _fromDoc(doc);
    save();
    return true;
}

bool Config::fromJson(const JsonDocument& doc) {
    if (isLocked()) return false;
    _fromDoc(doc);
    save();
    return true;
}

// ── Private helpers ───────────────────────────────────────────
void Config::_applyDefaults() {
    // All statics already have defaults set in their definitions above
}

void Config::_fromDoc(const JsonDocument& doc) {
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

    auto su = doc["sequence"]["startup"];
    startupOilArmTimeoutMs = su["oil_arm_timeout_ms"]      | startupOilArmTimeoutMs;
    startRpmThreshold      = su["start_rpm_threshold"]     | startRpmThreshold;
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
    timedDelayMs           = su["timed_delay_ms"]          | timedDelayMs;
    modifiedIdleMultiplier = su["modified_idle_multiplier"]| modifiedIdleMultiplier;
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
    totRiseRateLimitDegPerSec     = sf["tot_rise_rate_limit_deg_s"]  | totRiseRateLimitDegPerSec;
    titLimit                      = sf["tit_limit_c"]                | titLimit;
    oilTempLimit                  = sf["oil_temp_limit_c"]           | oilTempLimit;
    fuelPressMin                  = sf["fuel_press_min_bar"]         | fuelPressMin;
    battVoltMin                   = sf["batt_volt_min_v"]            | battVoltMin;
    surgeDetectRpmVariance        = sf["surge_detect_rpm_variance"]  | surgeDetectRpmVariance;

    auto gov = doc["governor"];
    governorTargetRpm  = gov["target_rpm"]   | governorTargetRpm;
    governorBandRpm    = gov["band_rpm"]      | governorBandRpm;
    governorKp         = gov["kp"]            | governorKp;
    governorPitchKp    = gov["pitch_kp"]      | governorPitchKp;
    propPitchIdleDeg   = gov["pitch_idle_deg"]| propPitchIdleDeg;
    propPitchMaxDeg    = gov["pitch_max_deg"] | propPitchMaxDeg;

    auto glw = doc["glow_plug"];
    glowPreheatMs      = glw["preheat_ms"]    | glowPreheatMs;
    glowPreheatMaxPct  = glw["preheat_max_pct"]| glowPreheatMaxPct;
    glowHoldPct        = glw["hold_pct"]      | glowHoldPct;

    auto cal = doc["calibration"];
    throttleMinRaw = cal["throttle_min_raw"] | throttleMinRaw;
    throttleMaxRaw = cal["throttle_max_raw"] | throttleMaxRaw;
    flameThreshold = cal["flame_threshold"]  | flameThreshold;

    auto poly = cal["oil_poly"];
    oilPolyA    = poly["a"]     | oilPolyA;
    oilPolyB    = poly["b"]     | oilPolyB;
    oilPolyC    = poly["c"]     | oilPolyC;
    oilPolyD    = poly["d"]     | oilPolyD;
    oilPolyXMin = poly["x_min"] | oilPolyXMin;
    oilPolyXMax = poly["x_max"] | oilPolyXMax;
    p1ZeroBar        = cal["p1_zero_bar"]       | p1ZeroBar;
    p2ZeroBar        = cal["p2_zero_bar"]       | p2ZeroBar;
    fuelPressRawMin  = cal["fuel_press_raw_min"]| fuelPressRawMin;
    fuelPressRawMax  = cal["fuel_press_raw_max"]| fuelPressRawMax;
    fuelPressValMax  = cal["fuel_press_val_max"]| fuelPressValMax;

    auto rl = doc["relight"];
    if (!rl["enabled"].isNull()) relightEnabled = rl["enabled"].as<bool>();
    relightMinRpm        = rl["min_rpm"]       | relightMinRpm;
    relightMaxAttempts   = rl["max_attempts"]  | relightMaxAttempts;

    auto tl = doc["tools"];
    toolFuelPrimeMs   = tl["fuel_prime_ms"]   | toolFuelPrimeMs;
    toolOilPrimeMs    = tl["oil_prime_ms"]    | toolOilPrimeMs;
    toolIgnTestMs     = tl["ign_test_ms"]     | toolIgnTestMs;
    toolStartTestMs   = tl["start_test_ms"]   | toolStartTestMs;
    toolFuelSolTestMs = tl["fuel_sol_test_ms"]| toolFuelSolTestMs;

    auto tm = doc["telemetry"];
    wsIntervalMs       = tm["ws_interval_ms"]       | wsIntervalMs;
    snapshotIntervalMs = tm["snapshot_interval_ms"] | snapshotIntervalMs;

    auto sa = doc["starter_assist"];
    starterAssistPct      = sa["pct"]           | starterAssistPct;
    starterAssistExitRpm  = sa["exit_rpm"]      | starterAssistExitRpm;
    starterRampPctPerSec  = sa["ramp_pct_per_s"]| starterRampPctPerSec;

    auto oilx = doc["oil_advanced"];
    oilZeroBar          = oilx["zero_bar"]      | oilZeroBar;
    oilPressureDeadband = oilx["deadband_bar"]  | oilPressureDeadband;

    // ignition section kept for forward compatibility but postIgnDwellMs removed

    auto sob = doc["standby_oil"];
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
    if (!ab["pump_follow_throttle"].isNull()) abPumpFollowThrottle = ab["pump_follow_throttle"].as<bool>();
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
        if (sl["throttle"] | false) mask |= SLOG_THR;
        if (sl["mode"]     | false) mask |= SLOG_MODE;
        sessionLogMask = mask;
        sessionLogIntervalMs = sl["interval_ms"] | sessionLogIntervalMs;
    }

    auto stats = doc["stats"];
    if (!stats.isNull()) {
        totalRunSeconds = stats["total_run_seconds"] | totalRunSeconds;
    }
}

void Config::_toDoc(JsonDocument& doc) {
    doc["profile_id"]     = OT_PROFILE_ID;
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
    su["start_rpm_threshold"]     = startRpmThreshold;
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
    su["timed_delay_ms"]           = timedDelayMs;
    su["modified_idle_multiplier"] = modifiedIdleMultiplier;
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
    gov["pitch_kp"]      = governorPitchKp;
    gov["pitch_idle_deg"]= propPitchIdleDeg;
    gov["pitch_max_deg"] = propPitchMaxDeg;

    auto glw = doc["glow_plug"].to<JsonObject>();
    glw["preheat_ms"]     = glowPreheatMs;
    glw["preheat_max_pct"]= glowPreheatMaxPct;
    glw["hold_pct"]       = glowHoldPct;

    auto cal = doc["calibration"].to<JsonObject>();
    cal["throttle_min_raw"] = throttleMinRaw;
    cal["throttle_max_raw"] = throttleMaxRaw;
    cal["flame_threshold"]  = flameThreshold;
    auto poly = cal["oil_poly"].to<JsonObject>();
    poly["a"]     = oilPolyA;
    poly["b"]     = oilPolyB;
    poly["c"]     = oilPolyC;
    poly["d"]     = oilPolyD;
    poly["x_min"] = oilPolyXMin;
    poly["x_max"]        = oilPolyXMax;
    cal["p1_zero_bar"]        = p1ZeroBar;
    cal["p2_zero_bar"]        = p2ZeroBar;
    cal["fuel_press_raw_min"] = fuelPressRawMin;
    cal["fuel_press_raw_max"] = fuelPressRawMax;
    cal["fuel_press_val_max"] = fuelPressValMax;

    auto rl = doc["relight"].to<JsonObject>();
    rl["enabled"]       = relightEnabled;
    rl["min_rpm"]       = relightMinRpm;
    rl["max_attempts"]  = relightMaxAttempts;

    auto tl = doc["tools"].to<JsonObject>();
    tl["fuel_prime_ms"]    = toolFuelPrimeMs;
    tl["oil_prime_ms"]     = toolOilPrimeMs;
    tl["ign_test_ms"]      = toolIgnTestMs;
    tl["start_test_ms"]    = toolStartTestMs;
    tl["fuel_sol_test_ms"] = toolFuelSolTestMs;

    auto tm = doc["telemetry"].to<JsonObject>();
    tm["ws_interval_ms"]       = wsIntervalMs;
    tm["snapshot_interval_ms"] = snapshotIntervalMs;

    auto sa = doc["starter_assist"].to<JsonObject>();
    sa["pct"]            = starterAssistPct;
    sa["exit_rpm"]       = starterAssistExitRpm;
    sa["ramp_pct_per_s"] = starterRampPctPerSec;

    auto oilx = doc["oil_advanced"].to<JsonObject>();
    oilx["zero_bar"]     = oilZeroBar;
    oilx["deadband_bar"] = oilPressureDeadband;


    auto sob = doc["standby_oil"].to<JsonObject>();
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
    ab["pump_follow_throttle"] = abPumpFollowThrottle;
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
    sl["interval_ms"]= sessionLogIntervalMs;

    auto stats = doc["stats"].to<JsonObject>();
    stats["total_run_seconds"] = totalRunSeconds;
}
