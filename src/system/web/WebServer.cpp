#include "WebServer.h"
#include "hardware_profile.h"
#include "../version.h"
#include "../Config.h"
#include "../HardwareConfig.h"
#include "../FlightRecorder.h"
#include "../SessionLogger.h"
#include "../../engine/EngineData.h"
#include "../../hal/sensors/AnalogSensor.h"

// Forward-declare the specific sensor globals needed for raw-ADC telemetry.
// Defined in main.cpp via OT_DECLARE_HARDWARE — including Hardware.h here would
// drag in every sequencer/controller header and cause ODR violations.
extern AnalogLinearSensor g_sensorP1;
extern AnalogLinearSensor g_sensorP2;
extern AnalogLinearSensor g_sensorBattVolt;
extern AnalogLinearSensor g_sensorGlowCurrent;
extern AnalogLinearSensor g_sensorIgniterCurrent;
extern AnalogLinearSensor g_sensorIgniter2Current;
extern AnalogLinearSensor g_sensorOilPumpCurrent;
extern AnalogLinearSensor g_sensorFuelFlow;
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Arduino.h>
#include <Update.h>
#include <new>

static volatile bool _otaPendingRestart      = false;
static volatile bool _otaInProgress          = false;
static bool          _otaError               = false;
static volatile bool _assetUploadInProgress  = false;
static bool          _assetUploadError       = false;
static AsyncWebServerRequest* _assetUploadOwner = nullptr;
static File          _assetTempFile;
static uint16_t      _assetUploadMask        = 0;
static unsigned long _assetUploadLastMs      = 0;
static AsyncWebServerRequest* _configRestoreOwner = nullptr;
static File          _configRestoreFile;
static bool          _configRestoreError     = false;
static unsigned long _configRestoreLastMs    = 0;
static volatile bool _hwRebootPending        = false;
static unsigned long _hwRebootScheduledMs    = 0;

// ── WebSocket telemetry state ─────────────────────────────────
// _wsPendingResponse: set when a "p" arrived but canSend() was false.
// tick() detects this and sends a WS PING; the PONG fires WS_EVT_PONG
// inside the async_tcp task (correct context), which then delivers the
// queued telemetry frame without waiting for another "p" from the client.
// _wsPendingFull: remembers whether the blocked frame should be a full frame
// (i.e. _fullCounter just rolled over to 0) so the PONG rescue honours it.
static volatile bool _wsPendingResponse = false;
static volatile bool _wsPendingFull     = false;
static unsigned long _wsPingMs          = 0;   // last ping timestamp

// LittleFS usage stats — cached by tick() every 10 s so _buildTelemetry
// is never called with a blocking filesystem operation while running inside
// the async_tcp task (would cause priority-inversion against webTask writes).
static uint32_t      s_fsTotal = 0;
static uint32_t      s_fsUsed  = 0;
static constexpr const char* FACTORY_CONFIG_PATH = "/factory_config.json";

// Shared buffers. Body handlers hold g_webRxOwner across all chunks so concurrent
// uploads cannot corrupt one another while RAM use remains bounded.
// Consolidating here (vs function-local statics) keeps them out of .bss individually and
// makes the total DRAM footprint explicit: 2 × 8192 = 16 KB instead of the ~68 KB that
// nine separate function-local statics would consume.
static char   g_webRxBuf[8192];   // request body accumulation (POST / PATCH)
static size_t g_webRxLen     = 0;
static bool   g_webRxOverflow = false;
static char   g_webTxBuf[8192];   // response serialisation + PATCH merge work buffer
static AsyncWebServerRequest* g_webRxOwner = nullptr;
static unsigned long g_webRxClaimMs = 0;

static void _mergeJsonObject(JsonObject dst, JsonObjectConst patch) {
    for (JsonPairConst kv : patch) {
        JsonVariantConst src = kv.value();
        if (src.is<JsonObjectConst>()) {
            JsonVariant nestedVariant = dst[kv.key()];
            JsonObject nested = nestedVariant.is<JsonObject>()
                ? nestedVariant.as<JsonObject>()
                : nestedVariant.to<JsonObject>();
            _mergeJsonObject(nested, src.as<JsonObjectConst>());
        } else {
            dst[kv.key()] = src;
        }
    }
}

static bool _claimWebRx(AsyncWebServerRequest* req, size_t index) {
    if (index == 0) {
        if (g_webRxOwner && (millis() - g_webRxClaimMs) < 10000) {
            req->send(409, "application/json", "{\"error\":\"Another upload is in progress\"}");
            return false;
        }
        g_webRxOwner = req;
        g_webRxClaimMs = millis();
        g_webRxLen = 0;
        g_webRxOverflow = false;
    }
    return g_webRxOwner == req;
}

static void _releaseWebRx(AsyncWebServerRequest* req) {
    if (g_webRxOwner == req) g_webRxOwner = nullptr;
}

static bool _outputsActiveForOta() {
    const auto& ed = EngineData::instance();
    return ed.extraCooldownActive || ed.standbyOilFeedActive ||
           ed.throttleDemand > 0.001f || ed.fuelPump2Demand > 0.001f ||
           ed.oilPumpPct > 0.01f || ed.starterDemand > 0.001f ||
           ed.abPumpDemand > 0.001f || ed.propPitchDemand > 0.001f ||
           ed.glowPlugDemand > 0.001f ||
           ed.fuelSolOpen || ed.igniterOn || ed.igniter2On ||
           ed.starterEnabled || ed.coolFanOn || ed.airstarterOpen ||
           ed.oilScavengeOn || ed.bleedValveOpen || ed.abSolOpen;
}

static const char* const WEB_ASSETS[] = {
    "app.js.gz", "calibration.html.gz", "config.html.gz", "hardware.html.gz",
    "index.html.gz", "log.html.gz", "sequence.html.gz", "style.css.gz",
    "tools.html.gz"
};
static constexpr uint16_t WEB_ASSET_COUNT = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]);
static constexpr uint16_t WEB_ASSET_ALL = (1u << WEB_ASSET_COUNT) - 1u;

static bool _maintenanceUploadInProgress() {
    return _otaInProgress || _assetUploadInProgress;
}

static int _assetIndex(String filename) {
    int slash = max(filename.lastIndexOf('/'), filename.lastIndexOf('\\'));
    if (slash >= 0) filename = filename.substring(slash + 1);
    for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
        if (filename == WEB_ASSETS[i]) return (int)i;
    }
    return -1;
}

static String _assetPath(uint16_t i, bool temp) {
    String path = "/";
    path += WEB_ASSETS[i];
    if (temp) path += ".upload";
    return path;
}

static String _assetBackupPath(uint16_t i) {
    String path = "/";
    path += WEB_ASSETS[i];
    path += ".backup";
    return path;
}

static void _discardAssetTemps() {
    if (_assetTempFile) _assetTempFile.close();
    for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
        String temp = _assetPath(i, true);
        if (LittleFS.exists(temp)) LittleFS.remove(temp);
    }
}

static void _finishAssetUpload() {
    _discardAssetTemps();
    _assetUploadOwner = nullptr;
    _assetUploadMask = 0;
    _assetUploadInProgress = false;
}

static void _recoverInterruptedAssetUpdate() {
    bool hasBackup = false;
    bool hasTemp = false;
    for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
        hasBackup |= LittleFS.exists(_assetBackupPath(i));
        hasTemp |= LittleFS.exists(_assetPath(i, true));
    }

    if (hasBackup && hasTemp) {
        Serial.println("[WebAssets] Interrupted swap detected - restoring previous pages");
        for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
            String target = _assetPath(i, false);
            String backup = _assetBackupPath(i);
            if (LittleFS.exists(backup)) {
                if (LittleFS.exists(target)) LittleFS.remove(target);
                LittleFS.rename(backup, target);
            }
        }
    } else if (hasBackup) {
        // Every staged file was installed before power was lost; discard old copies.
        Serial.println("[WebAssets] Completing installed page update cleanup");
        for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
            String backup = _assetBackupPath(i);
            if (LittleFS.exists(backup)) LittleFS.remove(backup);
        }
    }
    _discardAssetTemps();
}

static void _finishConfigRestore(bool discardTemp = true) {
    if (_configRestoreFile) _configRestoreFile.close();
    if (discardTemp) LittleFS.remove("/ecu_config.restore.tmp");
    _configRestoreOwner = nullptr;
    _configRestoreError = false;
}

static bool _writeUnifiedConfigAtomically(const JsonDocument& fullDoc) {
    static constexpr const char* TMP_PATH = "/ecu_config.restore.tmp";
    static constexpr const char* BAK_PATH = "/ecu_config.bak";
    if (!Config::acquireStorageWrite()) return false;
    struct StorageRelease {
        ~StorageRelease() { Config::releaseStorageWrite(); }
    } release;
    File fw = LittleFS.open(TMP_PATH, "w");
    if (!fw) return false;
    size_t expected = measureJsonPretty(fullDoc);
    bool wrote = serializeJsonPretty(fullDoc, fw) == expected;
    fw.close();
    if (!wrote) {
        LittleFS.remove(TMP_PATH);
        return false;
    }
    LittleFS.remove(BAK_PATH);
    bool hadOriginal = LittleFS.exists(Config::PATH);
    if (hadOriginal && !LittleFS.rename(Config::PATH, BAK_PATH)) {
        LittleFS.remove(TMP_PATH);
        return false;
    }
    if (!LittleFS.rename(TMP_PATH, Config::PATH)) {
        if (hadOriginal) LittleFS.rename(BAK_PATH, Config::PATH);
        return false;
    }
    if (hadOriginal) LittleFS.remove(BAK_PATH);
    return true;
}

static bool _copyLittleFsFile(const char* from, const char* to) {
    File src = LittleFS.open(from, "r");
    if (!src) return false;
    File dst = LittleFS.open(to, "w");
    if (!dst) {
        src.close();
        return false;
    }
    uint8_t buf[256];
    bool ok = true;
    while (src.available()) {
        size_t n = src.read(buf, sizeof(buf));
        if (dst.write(buf, n) != n) {
            ok = false;
            break;
        }
    }
    src.close();
    dst.close();
    if (!ok) LittleFS.remove(to);
    return ok;
}

class WebRxRelease {
public:
    explicit WebRxRelease(AsyncWebServerRequest* req) : _req(req) {}
    ~WebRxRelease() { _releaseWebRx(_req); }
private:
    AsyncWebServerRequest* _req;
};

static AsyncWebServer  _server(80);
static AsyncWebSocket  _ws("/ws");
static DNSServer       _dns;                 // captive portal DNS

static void _sendGzipAsset(AsyncWebServerRequest* req, const char* path,
                           const char* contentType, const char* cacheControl) {
    AsyncWebServerResponse* resp = req->beginResponse(LittleFS, path, contentType);
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control", cacheControl);
    resp->addHeader("Connection", "close");
    req->send(resp);
}

static bool _sendTelemetryFrame(AsyncWebSocketClient* client, const char* buf, size_t len) {
    // ESPAsyncWebServer copies each text frame into a heap-backed vector.
    // Avoid entering that allocator when RAM is already under pressure.
    static unsigned long lastDropLogMs = 0;
    const size_t reserve = len + 24576;
    if (!client || !client->canSend() ||
        ESP.getFreeHeap() <= reserve || ESP.getMaxAllocHeap() <= len + 8192) {
        if (millis() - lastDropLogMs >= 5000) {
            lastDropLogMs = millis();
            Serial.printf("[WebSocket] Telemetry deferred - low heap (frame=%u free=%u max_alloc=%u)\n",
                          (unsigned)len, (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getMaxAllocHeap());
        }
        return false;
    }
    return client->text(buf, len);
}


// ── WiFi AP setup ─────────────────────────────────────────────
static char _defaultApPwd[16] = {};   // per-device fallback, derived from eFuse MAC

static void _startWiFi() {
    WiFi.mode(WIFI_AP);
    const char* ssid = HardwareConfig::profileId[0] ? HardwareConfig::profileId : "OpenTurbine";
    // No configured password no longer selects an OPEN network: every API
    // endpoint is unauthenticated, so an open AP means anyone in RF range can
    // start the engine or flash firmware. Fall back to a per-device default
    // derived from the eFuse MAC — stable across boots and printed on the
    // serial console below so the owner can always recover it.
    const char* pwd = HardwareConfig::wifiPassword;
    if (!pwd[0]) {
        uint64_t chipId = ESP.getEfuseMac();
        snprintf(_defaultApPwd, sizeof(_defaultApPwd), "ot-%08lx",
                 (unsigned long)(chipId & 0xFFFFFFFFu));
        pwd = _defaultApPwd;
    }
    WiFi.softAP(ssid, pwd);  // SSID = hardware profile_id
    int8_t txPowerQdbm = (int8_t)constrain(HardwareConfig::wifiTxPowerDbm, 2, 20) * 4;
    esp_wifi_set_max_tx_power(txPowerQdbm);
    // Minimize WiFi power-save latency.  WIFI_PS_NONE keeps the ESP32 radio
    // always-on; DTIM=1 tells connected stations to wake at every beacon (~100 ms)
    // instead of the default every 3rd, preventing multi-second TCP stalls caused
    // by Windows/mobile WiFi adapters sleeping between beacons.
    esp_wifi_set_ps(WIFI_PS_NONE);
    {
        wifi_config_t ap_cfg;
        esp_wifi_get_config(WIFI_IF_AP, &ap_cfg);
        ap_cfg.ap.dtim_period = 1;
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    }
    IPAddress apIP = WiFi.softAPIP();
    if (HardwareConfig::wifiPassword[0]) {
        Serial.printf("[WiFi] AP: %s  IP: %s  (custom password)  TX=%d dBm\n",
                      ssid, apIP.toString().c_str(), (int)HardwareConfig::wifiTxPowerDbm);
    } else {
        Serial.printf("[WiFi] AP: %s  IP: %s  default password: %s  TX=%d dBm\n",
                      ssid, apIP.toString().c_str(), _defaultApPwd,
                      (int)HardwareConfig::wifiTxPowerDbm);
    }

    // Captive portal DNS — answers all DNS queries with our IP so phones
    // open the dashboard automatically when joining the AP.
    _dns.start(53, "*", apIP);
    Serial.println("[WiFi] Captive portal DNS started");

    // mDNS — accessible as http://ot.local on any mDNS-capable client
    if (MDNS.begin("ot")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[WiFi] mDNS: http://ot.local");
    }
}

// ── Telemetry JSON builder ────────────────────────────────────
// full=true  → complete frame: all fields (on WS_EVT_CONNECT and every ~30 s)
// full=false → fast frame: only real-time sensor/state fields (~2 KB vs ~7 KB)
//
// The JS keeps the last received value for every field, so omitting slow
// fields on fast frames has no visible effect after the first full frame.
static size_t _buildTelemetry(char* buf, size_t len, JsonDocument& doc, bool full) {
    auto& ed = EngineData::instance();
    doc.clear();

    // ── Fast fields — sent every pull cycle (~500 ms) ─────────────────────
    doc["mode"]                  = sysModeStr(ed.mode);
    doc["n1"]                    = (int)ed.n1Rpm;
    doc["n2"]                    = (int)ed.n2Rpm;
    doc["tot"]                   = (float)(int)(ed.tot * 10) / 10.0f;
    doc["tit"]                   = (float)(int)(ed.tit * 10) / 10.0f;
    doc["oil"]                   = (float)(int)(ed.oilPressure * 100) / 100.0f;
    doc["oil_raw"]               = ed.oilPressureRaw;
    doc["oil_demand"]            = (float)(int)(ed.oilTargetBar * 100) / 100.0f;
    doc["flame"]                 = ed.flameDetected;
    doc["flame_raw"]             = ed.flameSensorRaw;
    doc["torque_raw"]            = ed.torqueRaw;
    doc["p1"]                    = (float)(int)(std::max(0.0f, ed.p1 - Config::p1ZeroBar) * 100) / 100.0f;
    doc["p2"]                    = (float)(int)(std::max(0.0f, ed.p2 - Config::p2ZeroBar) * 100) / 100.0f;
    doc["p1_raw"]                = g_sensorP1.rawCounts();
    doc["p2_raw"]                = g_sensorP2.rawCounts();
    doc["max_p1"]                = (float)(int)(std::max(0.0f, ed.maxP1 - Config::p1ZeroBar) * 100) / 100.0f;
    doc["max_p2"]                = (float)(int)(std::max(0.0f, ed.maxP2 - Config::p2ZeroBar) * 100) / 100.0f;
    doc["fuel_pressure"]         = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_press_raw"]        = ed.fuelPressRaw;
    doc["fuel_flow"]             = (float)(int)(ed.fuelFlow * 100) / 100.0f;
    doc["fuel_flow_type"]        = HardwareConfig::fuelFlowType;
    doc["fuel_flow_raw"]         = (HardwareConfig::fuelFlowType == 0)
                                  ? g_sensorFuelFlow.rawCounts() : 0;
    doc["batt_voltage_raw"]      = g_sensorBattVolt.rawCounts();
    doc["glow_current_raw"]      = g_sensorGlowCurrent.rawCounts();
    doc["igniter_current_raw"]   = g_sensorIgniterCurrent.rawCounts();
    doc["igniter2_current_raw"]  = g_sensorIgniter2Current.rawCounts();
    doc["oil_pump_current_raw"]  = g_sensorOilPumpCurrent.rawCounts();
    // ── Throttle / idle demand ─────────────────────────────────────────────
    doc["throttle_input_raw"]    = ed.throttleInputRaw;
    {
        float inputNorm = 0.0f;
        if (HardwareConfig::throttleInputRcPwm) {
            inputNorm = ed.rcThrottleValid ? ed.rcThrottleNorm : 0.0f;
        } else {
            int range = Config::throttleMaxRaw - Config::throttleMinRaw;
            if (range != 0) inputNorm = constrain((ed.throttleInputRaw - Config::throttleMinRaw) /
                                                  (float)range, 0.0f, 1.0f);
        }
        doc["throttle_input_norm"] = (float)(int)(inputNorm * 1000) / 1000.0f;
    }
    doc["throttle_demand"]       = (float)(int)(ed.throttleDemand * 1000) / 1000.0f;
    // Effective throttle = pilot demand + AB main fuel offset (what the ESC sees)
    doc["throttle_effective"]    = (float)(int)(
        constrain(ed.throttleDemand + ed.abFuelOffset, 0.0f, 1.0f) * 1000) / 1000.0f;
    doc["ab_fuel_offset"]        = (float)(int)(ed.abFuelOffset * 1000) / 1000.0f;
    doc["idle_input_raw"]        = ed.idleInputRaw;
    doc["throttle_input_type"]   = !HardwareConfig::hasThrottleInput ? "none" :
                                   (HardwareConfig::throttleInputRcPwm ? "servo" : "adc");
    doc["idle_input_type"]       = !HardwareConfig::hasIdleInput ? "none" :
                                   (HardwareConfig::idleInputRcPwm ? "servo" : "adc");
    if (HardwareConfig::throttleInputRcPwm) doc["throttle_input_us"] = ed.throttleInputRaw;
    if (HardwareConfig::idleInputRcPwm)     doc["idle_input_us"]     = ed.idleInputRaw;
    doc["rc_throttle_valid"]     = ed.rcThrottleValid;
    doc["rc_throttle_norm"]      = (float)(int)(ed.rcThrottleNorm * 1000) / 1000.0f;
    doc["rc_idle_valid"]         = ed.rcIdleValid;
    doc["rc_idle_norm"]          = (float)(int)(ed.rcIdleNorm * 1000) / 1000.0f;
    // ── Health, actuators, switches ───────────────────────────────────────
    doc["oil_pct"]               = (int)ed.oilPumpPct;
    doc["n1_healthy"]            = ed.n1Healthy;
    doc["n2_healthy"]            = ed.n2Healthy;
    doc["tot_healthy"]           = ed.totHealthy;
    doc["tit_healthy"]           = ed.titHealthy;
    doc["oil_healthy"]           = ed.oilHealthy;
    doc["dynamic_idle_enabled"]  = ed.dynamicIdleEnabled;
    doc["idle_target_rpm"]       = Config::idleTargetRpm;
    doc["limp_mode"]             = ed.limpMode;
    doc["stop_switch_active"]    = ed.stopSwitchActive;
    doc["start_switch_active"]   = ed.startSwitchActive;
    doc["starter_assist_active"] = ed.starterAssistActive;
    doc["manual_relight_active"] = ed.manualRelightActive;
    doc["oil_failsafe_active"]   = ed.oilFailsafeActive;
    doc["oil_min_bar"]           = (float)(int)(ed.oilMinBar * 100) / 100.0f;
    doc["standby_oil_feed_active"] = ed.standbyOilFeedActive;
    doc["last_event"]            = ed.lastEvent;
    doc["dev_mode"]              = ed.devMode;
    doc["skip_safety_checks"]    = ed.skipSafetyChecks;
    doc["bench_mode"]            = ed.benchMode;
    doc["ota_in_progress"]       = _maintenanceUploadInProgress();
    doc["relight_armed"]         = ed.relightArmed;
    doc["relight_attempts"]      = (int)ed.relightAttempts;
    doc["extra_cooldown_active"] = ed.extraCooldownActive;
    {
        unsigned long now = millis();
        long remainingMs = (long)(ed.extraCooldownUntilMs - now);
        int remS = (ed.extraCooldownActive && remainingMs > 0)
                   ? (int)((unsigned long)remainingMs / 1000UL) : 0;
        doc["extra_cooldown_remaining_s"] = remS;
    }
    doc["profile_match"]         = Config::profileMatch;
    doc["config_version_mismatch"] = ed.configVersionMismatch;
    doc["fw_version"]            = OT_VERSION;
    doc["uptime_s"]              = ed.uptimeMs / 1000;
    doc["log_records"]           = FlightRecorder::recordCount();
    doc["max_n1"]                = (int)ed.maxN1;
    doc["max_n2"]                = (int)ed.maxN2;
    doc["max_tot"]               = (float)(int)(ed.maxTot * 10) / 10.0f;
    doc["tot_rise_rate"]         = (float)(int)(ed.totRiseRate * 10) / 10.0f;
    doc["surge_detected"]        = ed.surgeDetected;
    // ── Afterburner runtime state ──────────────────────────────────────────
    {
        const char* abStr = "Off";
        switch (ed.abMode) {
            case ABMode::Off:         abStr = "Off";          break;
            case ABMode::Arming:      abStr = "Arming";       break;
            case ABMode::Igniting:    abStr = "Igniting";     break;
            case ABMode::Running:     abStr = "Running";      break;
            case ABMode::ShuttingDown:abStr = "ShuttingDown"; break;
            case ABMode::Fault:       abStr = "Fault";        break;
        }
        doc["ab_mode"]           = abStr;
    }
    doc["ab_trigger_active"]     = ed.abTriggerActive;
    doc["ab_arm_switch_on"]      = ed.abArmSwitchOn;
    doc["ab_flame_on"]           = ed.abFlameOn;
    doc["ab_sol_open"]           = ed.abSolOpen;
    // ── Sequence progress + fault ─────────────────────────────────────────
    doc["current_block"]         = ed.currentBlock;
    doc["seq_block_idx"]         = (int)ed.seqBlockIdx;
    doc["seq_block_total"]       = (int)ed.seqBlockTotal;
    doc["seq_wait_reason"]       = ed.seqWaitReason[0] ? ed.seqWaitReason : nullptr;
    doc["fault_description"]     = ed.faultDescription;
    // ── Extended sensor values (has_* flags are in the slow section) ───────
    doc["oil_temp"]              = (float)(int)(ed.oilTemp * 10) / 10.0f;
    doc["oil_temp_healthy"]      = ed.oilTempHealthy;
    doc["max_oil_temp"]          = (float)(int)(ed.maxOilTemp * 10) / 10.0f;
    doc["batt_voltage"]          = (float)(int)(ed.battVoltage * 100) / 100.0f;
    doc["batt_healthy"]          = ed.battHealthy;
    doc["max_batt_voltage"]      = (float)(int)(ed.maxBattVoltage * 100) / 100.0f;
    doc["torque"]                = (float)(int)(ed.torque * 10) / 10.0f;
    doc["turbo_power_w"]         = (int)ed.turboPower;
    doc["torque_healthy"]        = ed.torqueHealthy;
    doc["fuel_press"]            = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_press_healthy"]    = ed.fuelPressHealthy;
    doc["max_fuel_press"]        = (float)(int)(ed.maxFuelPressure * 100) / 100.0f;
    doc["glow_plug_pct"]         = (int)(ed.glowPlugDemand * 100.0f);
    doc["glow_plug_hot"]         = ed.glowPlugHot;
    doc["glow_current_amps"]     = (float)(int)(ed.glowCurrentAmps * 10) / 10.0f;
    doc["igniter_current_amps"]  = (float)(int)(ed.igniterCurrentAmps  * 10) / 10.0f;
    doc["igniter2_current_amps"] = (float)(int)(ed.igniter2CurrentAmps * 10) / 10.0f;
    doc["oil_pump_current_amps"] = (float)(int)(ed.oilPumpCurrentAmps  * 10) / 10.0f;
    doc["oil_pump_overcurrent"]  = ed.oilPumpOvercurrent;
    doc["bleed_valve_open"]      = ed.bleedValveOpen;
    doc["prop_pitch_demand"]     = (float)(int)(ed.propPitchDemand * 1000) / 1000.0f;
    doc["fuel_pump2_demand"]     = (float)(int)(ed.fuelPump2Demand * 1000) / 1000.0f;
    doc["cool_fan_on"]           = ed.coolFanOn;
    doc["airstarter_open"]       = ed.airstarterOpen;
    doc["oil_scavenge_on"]       = ed.oilScavengeOn;
    doc["governor_target_rpm"]   = (int)Config::governorTargetRpm;
    doc["max_tit"]               = (float)(int)(ed.maxTit * 10) / 10.0f;
    // ── DI channel states (config fields — pin/label/role — are in slow) ──
    {
        auto diArr = doc["di_channels"].to<JsonArray>();
        for (int i = 0; i < HardwareConfig::MAX_DI; i++) {
            auto ch = diArr.add<JsonObject>();
            ch["state"] = ed.diState[i];
            ch["pin"]   = HardwareConfig::diCh[i].pin;  // needed by JS show/hide logic
        }
    }

    // ── Slow fields — sent on connect + every ~30 s ───────────────────────
    // Hardware config flags, safety limits, labels, calibration raw values,
    // boot/session stats.  These never change during normal engine operation.
    if (full) {
        doc["has_fuel_flow"]         = HardwareConfig::hasFuelFlow;
        doc["pressure_sensors_enabled"] = Config::pressureSensorsEnabled;
        doc["flame_threshold"]       = Config::flameThreshold;
        // Input type strings (hardware topology — doesn't change at runtime)
        doc["rc_pwm_active"]         = HardwareConfig::throttleInputRcPwm
                                       || HardwareConfig::idleInputRcPwm;
        doc["idle_use_n2"]           = Config::idleUseN2;
        doc["limp_throttle_cap"]     = Config::limpMaxThrottlePct;
        doc["idle_min_pct"]          = Config::throttleIdleMinPct;
        doc["fuel_idle_min_pct"]     = Config::fuelPumpIdleMinPct;
        doc["fuel_idle_max_pct"]     = Config::fuelPumpIdleMaxPct;
        doc["oil_pump_on_pct"]       = Config::oilPumpOnPct;
        doc["has_throttle"]          = HardwareConfig::hasThrottle;
        doc["has_oil_pump"]          = HardwareConfig::hasOilPump;
        doc["has_dynamic_idle"]      = HardwareConfig::hasDynamicIdle;
        doc["ws_interval_ms"]        = Config::wsIntervalMs;
        doc["has_oil_loop"]          = HardwareConfig::instance().hasOilLoop;
        doc["relight_enabled"]       = Config::relightEnabled;
        doc["flameout_source"]       = Config::flameoutSource;
        doc["flameout_n1_min_rpm"]   = Config::flameoutN1MinRpm;
        doc["flameout_tot_drop_c"]   = Config::flameoutTotDropC;
        doc["relight_confirm_source"] = Config::relightConfirmSource;
        doc["relight_min_rpm"]       = Config::relightMinRpm;
        doc["relight_confirm_rpm"]   = Config::relightConfirmRpm;
        doc["relight_tot_rise_c"]    = Config::relightTotRiseC;
        doc["dev_mode_fw"]           = true;
        doc["config_locked"]         = Config::isLocked();
        doc["config_storage_fault"]  = ed.configLocked;
        doc["config_version_firmware"] = Config::CONFIG_VERSION;
        doc["hardware_profile"]      = HardwareConfig::profileId;
        doc["hw_json_loaded"]        = true;
        // Session / boot stats
        doc["run_count"]             = ed.runCount;
        doc["boot_count"]            = ed.bootCount;
        doc["reset_reason"]          = ed.resetReason;
        doc["total_run_seconds"]     = Config::totalRunSeconds;
        // Flash usage (cached by tick() — never call LittleFS from async_tcp context)
        doc["log_max_records"]       = FlightRecorder::MAX_RECORDS;
        doc["flash_total_kb"]        = (int)s_fsTotal;
        doc["flash_used_kb"]         = (int)s_fsUsed;
        doc["flash_free_kb"]         = (int)(s_fsTotal - s_fsUsed);
        doc["max_p1"]                = (float)(int)(std::max(0.0f, ed.maxP1 - Config::p1ZeroBar) * 100) / 100.0f;
        doc["max_p2"]                = (float)(int)(std::max(0.0f, ed.maxP2 - Config::p2ZeroBar) * 100) / 100.0f;
        // Safety limits (for color gauge thresholds)
        doc["rpm_limit"]             = (int)Config::rpmLimit;
        doc["tot_limit"]             = Config::totLimit;
        doc["oil_running_min"]       = Config::oilRunningMin;
        doc["oil_temp_limit"]        = Config::oilTempLimit;
        doc["tit_limit"]             = Config::titLimit;
        doc["batt_volt_min"]         = Config::battVoltMin;
        doc["fuel_press_min"]        = Config::fuelPressMin;
        // has_* capability flags
        doc["has_afterburner"]       = HardwareConfig::hasAfterburner;
        doc["has_ab_flame"]          = HardwareConfig::hasAbFlame;
        doc["has_n1"]                = HardwareConfig::hasN1Rpm;
        doc["has_n2"]                = HardwareConfig::hasN2Rpm;
        doc["has_tot"]               = HardwareConfig::hasTot;
        doc["has_oil_press"]         = HardwareConfig::hasOilPress;
        doc["has_flame"]             = HardwareConfig::hasFlame;
        doc["has_p1"]                = HardwareConfig::hasP1;
        doc["has_p2"]                = HardwareConfig::hasP2;
        doc["has_oil_temp"]          = HardwareConfig::hasOilTemp;
        doc["has_batt_voltage"]      = HardwareConfig::hasBattVoltage;
        doc["has_torque"]            = HardwareConfig::hasTorque;
        doc["has_fuel_press"]        = HardwareConfig::hasFuelPress;
        doc["has_governor"]          = HardwareConfig::hasGovernor;
        doc["has_glow_plug"]         = HardwareConfig::hasGlowPlug;
        doc["has_glow_current"]      = HardwareConfig::hasGlowCurrentSensor;
        doc["has_igniter_current"]   = HardwareConfig::hasIgniterCurrentSensor;
        doc["has_igniter2_current"]  = HardwareConfig::hasIgniter2CurrentSensor;
        doc["has_oil_pump_current"]  = HardwareConfig::hasOilPumpCurrentSensor;
        doc["has_bleed_valve"]       = HardwareConfig::hasBleedValve;
        doc["has_prop_pitch"]        = HardwareConfig::hasPropPitch;
        doc["has_fuel_pump2"]        = HardwareConfig::hasFuelPump2;
        doc["has_cool_fan"]          = HardwareConfig::hasCoolFan;
        doc["has_airstarter"]        = HardwareConfig::hasAirstarterSol;
        doc["has_oil_scavenge"]      = HardwareConfig::hasOilScavengePump;
        doc["has_mavlink"]           = HardwareConfig::hasMAVLink;
        doc["has_tit"]               = HardwareConfig::hasTit;
        doc["has_starter_assist"]    = HardwareConfig::starterAssistEnabled
                                       && (HardwareConfig::starterType != 2);
        // Calibration / raw ADC (used by calibration page via /api/data REST)
        doc["igniter_coil"]          = HardwareConfig::igniterCoil;
        // ── Channel labels ────────────────────────────────────────────────
        auto tlbl = doc["labels"].to<JsonObject>();
        tlbl["tot"]        = HardwareConfig::labelTot;
        tlbl["tit"]        = HardwareConfig::labelTit;
        tlbl["n1"]         = HardwareConfig::labelN1;
        tlbl["n2"]         = HardwareConfig::labelN2;
        tlbl["oil_press"]  = HardwareConfig::labelOilPress;
        tlbl["oil_temp"]   = HardwareConfig::labelOilTemp;
        tlbl["p1"]         = HardwareConfig::labelP1;
        tlbl["p2"]         = HardwareConfig::labelP2;
        tlbl["fuel_press"] = HardwareConfig::labelFuelPress;
        tlbl["fuel_flow"]  = HardwareConfig::labelFuelFlow;
        tlbl["stop"]       = HardwareConfig::labelStop;
        tlbl["start"]      = HardwareConfig::labelStart;
        tlbl["ab_arm"]     = HardwareConfig::labelAbArm;
        // ── Sequence validation issues ────────────────────────────────────
        doc["seq_has_errors"] = ed.seqHasErrors;
        auto issArr = doc["seq_issues"].to<JsonArray>();
        for (int i = 0; i < ed.seqIssueCount; i++) {
            auto obj = issArr.add<JsonObject>();
            obj["block"] = ed.seqIssues[i].blockName;
            obj["msg"]   = ed.seqIssues[i].reason;
            obj["error"] = ed.seqIssues[i].isError;
        }
        // ── DI channel config (label / role — state + pin already in fast) ──
        // Overwrite the array built above with the full per-channel objects.
        auto diArr = doc["di_channels"].to<JsonArray>();
        for (int i = 0; i < HardwareConfig::MAX_DI; i++) {
            auto ch = diArr.add<JsonObject>();
            ch["state"] = ed.diState[i];
            ch["pin"]   = HardwareConfig::diCh[i].pin;
            if (HardwareConfig::diCh[i].label[0]) {
                ch["label"] = HardwareConfig::diCh[i].label;
            } else {
                char lbuf[8];
                snprintf(lbuf, sizeof(lbuf), "DI-%d", i + 1);
                ch["label"] = lbuf;  // ArduinoJson copies char* (non-const ptr)
            }
            ch["role"] = HardwareConfig::diCh[i].role;
        }
    }
    return serializeJson(doc, buf, len);
}

// ── Route setup ───────────────────────────────────────────────
void WebServer::_setupRoutes() {
    // ── Captive portal redirect ───────────────────────────────
    // Phones check connectivity by fetching well-known URLs (generate_204,
    // hotspot-detect.html, etc.).  Any request whose Host header is not our
    // IP or "ot.local" gets a 302 → dashboard so the OS pops up the captive
    // portal browser automatically.
    auto isCaptive = [](AsyncWebServerRequest* req) -> bool {
        String host = req->host();
        // allow direct IP and our mDNS hostname
        if (host == WiFi.softAPIP().toString()) return false;
        if (host == "ot.local")                 return false;
        return true;
    };
    auto redirectCaptiveToIp = [isCaptive](AsyncWebServerRequest* req) -> bool {
        if (!isCaptive(req)) return false;
        String target = "http://";
        target += WiFi.softAPIP().toString();
        target += req->url();
        req->redirect(target);
        return true;
    };

    // ── Captive portal landing ─────────────────────────────────
    // Serve the portal body directly for OS probes. Windows may follow a redirect
    // through its own /redirect target and end up at msn.com instead of our page.
    auto sendPortalPage = [](AsyncWebServerRequest* req) {
        _sendGzipAsset(req, "/index.html.gz", "text/html", "no-store");
    };
    _server.on("/generate_204", HTTP_GET, sendPortalPage);
    _server.on("/hotspot-detect.html", HTTP_GET, sendPortalPage);
    _server.on("/library/test/success.html", HTTP_GET, sendPortalPage);
    _server.on("/connecttest.txt", HTTP_GET, sendPortalPage);
    _server.on("/ncsi.txt", HTTP_GET, sendPortalPage);
    _server.on("/fwlink", HTTP_GET, sendPortalPage);
    _server.on("/redirect", HTTP_GET, sendPortalPage);
    _server.on("/canonical.html", HTTP_GET, sendPortalPage);

    // app.js and style.css are shared across all pages and only change when the
    // filesystem is reflashed — cache them for 1 hour to avoid re-fetching on every
    // page navigation, which would exhaust the lwIP TCP PCB pool alongside the
    // persistent SSE connection and cause "IP not responding" errors.
    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendGzipAsset(req, "/app.js.gz", "application/javascript", "no-cache");
    });
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendGzipAsset(req, "/style.css.gz", "text/css", "no-cache");
    });
    _server.on("/", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/index.html.gz", "text/html", "no-cache");
    });
    _server.on("/index.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/index.html.gz", "text/html", "no-cache");
    });
    _server.on("/hardware.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/hardware.html.gz", "text/html", "no-cache");
    });
    _server.on("/calibration.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/calibration.html.gz", "text/html", "no-cache");
    });
    _server.on("/config.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/config.html.gz", "text/html", "no-cache");
    });
    _server.on("/sequence.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/sequence.html.gz", "text/html", "no-cache");
    });
    _server.on("/log.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/log.html.gz", "text/html", "no-cache");
    });
    _server.on("/tools.html", HTTP_GET, [redirectCaptiveToIp](AsyncWebServerRequest* req) {
        if (redirectCaptiveToIp(req)) return;
        _sendGzipAsset(req, "/tools.html.gz", "text/html", "no-cache");
    });
    _server.on("/ecu_config.json", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(403, "text/plain", "Forbidden");
    });
    _server.on("/hardware.json", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(403, "text/plain", "Forbidden");
    });
    // HTML pages: no-cache so a filesystem reflash is always picked up immediately.
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");

    // GET /api/data — live snapshot. Uses g_webTxBuf (static) to avoid a 6 KB stack
    // allocation inside the async TCP task callback (task stack is ~8 KB).
    _server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        static JsonDocument doc;   // static: avoids re-allocating ArduinoJson heap every call
        _buildTelemetry(g_webTxBuf, sizeof(g_webTxBuf), doc, true);
        req->send(200, "application/json", g_webTxBuf);
    });

    // GET /api/status
    _server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto& ed = EngineData::instance();
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"mode\":\"%s\",\"locked\":%s,\"profile_match\":%s}",
            sysModeStr(ed.mode),
            Config::isLocked() ? "true" : "false",
            Config::profileMatch ? "true" : "false");
        req->send(200, "application/json", buf);
    });

    // GET /api/config — expose the settings section for page editors
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t n = Config::toJson(g_webTxBuf, sizeof(g_webTxBuf));
        if (n >= sizeof(g_webTxBuf)) Serial.printf("[WebServer] WARNING: config JSON truncated (%u >= %u)\n", (unsigned)n, (unsigned)sizeof(g_webTxBuf));
        req->send(200, "application/json", g_webTxBuf);
    });

    // POST /api/config — replace only the settings section in ecu_config.json.
    // Body is accumulated across chunks before parsing — this section is ~2-3 KB
    // and may arrive in multiple TCP segments.
    _server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_claimWebRx(req, index)) return;
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;   // wait for more chunks
            WebRxRelease release(req);
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }
            if (!Config::isLocked()) {
                bool ok = Config::fromJson(g_webRxBuf, g_webRxLen);
                // If fromJson succeeds it already saves; do NOT save on failure — it would
                // silently persist the old (unchanged) config and mislead the caller.
                Serial.printf("[WebServer] POST /api/config: len=%u ok=%d\n", (unsigned)g_webRxLen, ok ? 1 : 0);
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"settings rejected — check JSON and loaded engine profile_id\"}");
            } else {
                req->send(423, "application/json", "{\"error\":\"locked\"}");
            }
        });

    // PATCH /api/config — partial update to the settings section.
    // Merges incoming JSON over the current settings and saves the unified engine file.
    _server.on("/api/config", HTTP_PATCH,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_claimWebRx(req, index)) return;
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;   // wait for more chunks
            WebRxRelease release(req);
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }
            if (Config::isLocked()) {
                req->send(423, "application/json", "{\"error\":\"locked\"}");
                return;
            }
            JsonDocument patch;
            if (deserializeJson(patch, g_webRxBuf, g_webRxLen) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            // Load current config into a document, merge patch on top, re-apply.
            // Recursive merge keeps sibling fields inside nested sections.
            JsonDocument current;
            Config::toJson(current);
            _mergeJsonObject(current.as<JsonObject>(), patch.as<JsonObjectConst>());
            bool ok = Config::fromJson(current);
            CommandQueue::push({ OTCommand::APPLY_CONFIG });
            FlightRecorder::logConfigChange("config.patch", 0, 0);
            // Config values are live in memory immediately.
            // Block-instance params (applyConfig) are applied on next START; warn if deferred.
            bool deferred = (EngineData::instance().mode != SysMode::STANDBY);
            if (!ok) {
                req->send(500, "application/json", "{\"ok\":false,\"error\":\"flash write failed\"}");
            } else if (deferred) {
                req->send(200, "application/json",
                    "{\"ok\":true,\"warn\":\"config saved; block params will apply on next engine start\"}");
            } else {
                req->send(200, "application/json", "{\"ok\":true}");
            }
        });

    // GET /api/log — last 400 events as a JSON array for in-browser display.
    // Capped so AsyncResponseStream never buffers more than ~32 KB regardless of log size.
    // For a full download use /api/log/raw (served directly from flash, zero heap buffer).
    _server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (EngineData::instance().mode != SysMode::STANDBY || Config::logStandby) {
            req->send(423, "application/json",
                "{\"error\":\"Log viewing requires STANDBY with standby logging disabled\"}");
            return;
        }
        const int DISPLAY_LIMIT = 400;
        AsyncResponseStream* resp = req->beginResponseStream("application/json");
        FlightRecorder::lockLog();
        File f = LittleFS.open(FlightRecorder::PATH, "r");
        int total = FlightRecorder::recordCount();
        int skip  = total > DISPLAY_LIMIT ? total - DISPLAY_LIMIT : 0;
        resp->print('[');
        bool first = true;
        int  seen  = 0;
        if (f) {
            char lineBuf[320];
            while (f.available()) {
                int n = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                if (n <= 0) continue;
                if (lineBuf[n - 1] == '\r') n--;
                lineBuf[n] = '\0';
                if (n == 0 || lineBuf[0] != '{') continue;
                if (seen++ < skip) continue;
                if (!first) resp->print(',');
                first = false;
                resp->print(lineBuf);
            }
            f.close();
        }
        resp->print(']');
        FlightRecorder::unlockLog();
        req->send(resp);
    });

    // GET /api/log/raw — full flight log download as NDJSON (one JSON object per line).
    // Uses AsyncFileResponse: reads LittleFS in 1460-byte TCP chunks without heap buffering.
    _server.on("/api/log/raw", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (EngineData::instance().mode != SysMode::STANDBY || Config::logStandby) {
            req->send(423, "application/json",
                "{\"error\":\"Raw log download requires STANDBY with standby logging disabled\"}");
            return;
        }
        if (!LittleFS.exists(FlightRecorder::PATH)) {
            req->send(404, "text/plain", "No log");
            return;
        }
        FlightRecorder::beginRawDownload();
        req->onDisconnect([]() { FlightRecorder::endRawDownload(); });
        AsyncWebServerResponse* resp = req->beginResponse(
            LittleFS, FlightRecorder::PATH, "application/x-ndjson");
        resp->addHeader("Content-Disposition", "attachment; filename=\"flight_log.ndjson\"");
        req->send(resp);
    });

    // GET /api/log/csv — flat CSV download, streamed from flash
    _server.on("/api/log/csv", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (EngineData::instance().mode != SysMode::STANDBY || Config::logStandby) {
            req->send(423, "application/json",
                "{\"error\":\"Log download requires STANDBY with standby logging disabled\"}");
            return;
        }
        AsyncResponseStream* resp = req->beginResponseStream("text/csv");
        resp->addHeader("Content-Disposition", "attachment; filename=\"flight_log.csv\"");
        resp->print("t,ev,details\r\n");
        FlightRecorder::lockLog();
        File f = LittleFS.open(FlightRecorder::PATH, "r");
        if (f) {
            JsonDocument doc;   // declared once outside the loop — avoids 2200× heap alloc/free
            char lineBuf[320];
            while (f.available()) {
                int n = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                if (n <= 0) continue;
                if (lineBuf[n - 1] == '\r') n--;
                lineBuf[n] = '\0';
                if (n == 0 || lineBuf[0] != '{') continue;
                doc.clear();
                if (deserializeJson(doc, lineBuf)) continue;
                unsigned long t  = doc["t"] | 0UL;
                const char*   ev = doc["ev"] | "";
                char detail[220] = {};
                int  dpos = 0;
                for (JsonPair kv : doc.as<JsonObject>()) {
                    if (strcmp(kv.key().c_str(), "t")  == 0) continue;
                    if (strcmp(kv.key().c_str(), "ev") == 0) continue;
                    if (dpos > 0 && dpos < (int)sizeof(detail) - 1) detail[dpos++] = ' ';
                    dpos += snprintf(detail + dpos, sizeof(detail) - dpos,
                                     "%s=%s", kv.key().c_str(),
                                     kv.value().as<const char*>() ? kv.value().as<const char*>()
                                                                   : kv.value().as<String>().c_str());
                    if (dpos >= (int)sizeof(detail) - 1) break;
                }
                char row[256];
                snprintf(row, sizeof(row), "%lu,\"%s\",\"%s\"\r\n", t, ev, detail);
                resp->print(row);
            }
            f.close();
        }
        FlightRecorder::unlockLog();
        req->send(resp);
    });

    // POST /api/start
    _server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (_maintenanceUploadInProgress()) {
            req->send(423, "application/json", "{\"ok\":false,\"error\":\"Maintenance upload in progress\"}");
            return;
        }
        auto& ed = EngineData::instance();
        if (ed.stopSwitchActive) {
            snprintf(ed.lastEvent, sizeof(ed.lastEvent), "START blocked: stop switch active");
            snprintf(ed.faultDescription, sizeof(ed.faultDescription),
                     "Cannot start: STOP switch is active. Release STOP before pressing START.");
            req->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"STOP switch is active. Release STOP before pressing START.\"}");
            return;
        }
        if (CommandQueue::push({ OTCommand::START })) {
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(503, "application/json", "{\"ok\":false,\"error\":\"Command queue full\"}");
        }
    });

    // POST /api/stop
    _server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (CommandQueue::pushEmergencyStop({ OTCommand::STOP })) {
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(503, "application/json", "{\"ok\":false,\"error\":\"STOP could not be queued\"}");
        }
    });

    // POST /api/command — generic command dispatch
    _server.on("/api/command", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_claimWebRx(req, index)) return;
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;
            WebRxRelease release(req);
            if (g_webRxOverflow) { req->send(400, "application/json", "{\"error\":\"request too large\"}"); return; }
            static JsonDocument doc;   // static: one allocation reused across all /api/command calls
            doc.clear();
            if (deserializeJson(doc, g_webRxBuf, g_webRxLen)) {
                req->send(400); return;
            }
            const char* cmdStr = doc["cmd"] | "";
            OTPacket pkt;
            if      (strcmp(cmdStr, "FUEL_PRIME")     == 0) pkt.cmd = OTCommand::FUEL_PRIME;
            else if (strcmp(cmdStr, "OIL_PRIME")      == 0) pkt.cmd = OTCommand::OIL_PRIME;
            else if (strcmp(cmdStr, "IGN_TEST")       == 0) pkt.cmd = OTCommand::IGN_TEST;
            else if (strcmp(cmdStr, "IGN2_TEST")      == 0) pkt.cmd = OTCommand::IGN2_TEST;
            else if (strcmp(cmdStr, "START_TEST")     == 0) pkt.cmd = OTCommand::START_TEST;
            else if (strcmp(cmdStr, "FUEL_SOL_TEST")        == 0) pkt.cmd = OTCommand::FUEL_SOL_TEST;
            else if (strcmp(cmdStr, "IDLE_TEST")            == 0) pkt.cmd = OTCommand::IDLE_TEST;
            else if (strcmp(cmdStr, "TOGGLE_DYNAMIC_IDLE")  == 0) pkt.cmd = OTCommand::TOGGLE_DYNAMIC_IDLE;
            else if (strcmp(cmdStr, "TOGGLE_LIMP_MODE")     == 0) pkt.cmd = OTCommand::TOGGLE_LIMP_MODE;
            else if (strcmp(cmdStr, "TOGGLE_DEV_MODE")        == 0) pkt.cmd = OTCommand::TOGGLE_DEV_MODE;
            else if (strcmp(cmdStr, "TOGGLE_SAFETY_CHECKS")  == 0) pkt.cmd = OTCommand::TOGGLE_SAFETY_CHECKS;
            else if (strcmp(cmdStr, "TOGGLE_BENCH_MODE")     == 0) pkt.cmd = OTCommand::TOGGLE_BENCH_MODE;
            else if (strcmp(cmdStr, "SET_OIL_PCT")          == 0) pkt.cmd = OTCommand::SET_OIL_PCT;
            else if (strcmp(cmdStr, "SET_OIL_DEMAND")        == 0) pkt.cmd = OTCommand::SET_OIL_DEMAND;
            else if (strcmp(cmdStr, "EXTRA_COOLDOWN")        == 0) pkt.cmd = OTCommand::EXTRA_COOLDOWN;
            else if (strcmp(cmdStr, "STARTER_ASSIST")       == 0) pkt.cmd = OTCommand::STARTER_ASSIST;
            else if (strcmp(cmdStr, "CLEAR_LOG")            == 0) pkt.cmd = OTCommand::CLEAR_LOG;
            else if (strcmp(cmdStr, "AB_FIRE")              == 0) pkt.cmd = OTCommand::AB_FIRE;
            else if (strcmp(cmdStr, "AB_STOP")              == 0) pkt.cmd = OTCommand::AB_STOP;
            else if (strcmp(cmdStr, "OIL_SCAV_TEST")        == 0) pkt.cmd = OTCommand::OIL_SCAV_TEST;
            else if (strcmp(cmdStr, "COOL_FAN_TEST")        == 0) pkt.cmd = OTCommand::COOL_FAN_TEST;
            else if (strcmp(cmdStr, "AIRSTARTER_TEST")      == 0) pkt.cmd = OTCommand::AIRSTARTER_TEST;
            else if (strcmp(cmdStr, "BLEED_VALVE_TEST")     == 0) pkt.cmd = OTCommand::BLEED_VALVE_TEST;
            else if (strcmp(cmdStr, "GLOW_TEST")            == 0) pkt.cmd = OTCommand::GLOW_TEST;
            else if (strcmp(cmdStr, "FUEL_PUMP2_TEST")      == 0) pkt.cmd = OTCommand::FUEL_PUMP2_TEST;
            else if (strcmp(cmdStr, "AB_SOL_TEST")          == 0) pkt.cmd = OTCommand::AB_SOL_TEST;
            else if (strcmp(cmdStr, "AB_PUMP_TEST")         == 0) pkt.cmd = OTCommand::AB_PUMP_TEST;
            else if (strcmp(cmdStr, "STARTER_EN_TEST")      == 0) pkt.cmd = OTCommand::STARTER_EN_TEST;
            else if (strcmp(cmdStr, "PROP_PITCH_TEST")      == 0) pkt.cmd = OTCommand::PROP_PITCH_TEST;
            else if (strcmp(cmdStr, "RESET_PEAKS")          == 0) pkt.cmd = OTCommand::RESET_PEAKS;
            else { req->send(400); return; }
            pkt.fParam = doc["fParam"] | 0.0f;
            pkt.iParam = doc["iParam"] | 0;
            if (_maintenanceUploadInProgress() && pkt.cmd != OTCommand::AB_STOP) {
                req->send(423, "application/json", "{\"ok\":false,\"error\":\"Maintenance upload in progress\"}");
                return;
            }
            bool queued = pkt.cmd == OTCommand::AB_STOP
                        ? CommandQueue::pushFront(pkt) : CommandQueue::push(pkt);
            if (queued) {
                req->send(200, "application/json", "{\"ok\":true}");
            } else {
                req->send(503, "application/json", "{\"ok\":false,\"error\":\"Command queue full\"}");
            }
        });

    // DELETE /api/session/all — wipe every session_N.csv file from /logs
    _server.on("/api/session/all", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        File dir = LittleFS.open("/logs");
        if (dir) {
            File entry = dir.openNextFile();
            while (entry) {
                int num = -1;
                // entry.name() may return the full path (/logs/session_1.csv) or just the
                // basename (session_1.csv) depending on LittleFS version — strip the dir prefix.
                const char* ename = entry.name();
                const char* fname = strrchr(ename, '/');
                fname = fname ? fname + 1 : ename;
                if (sscanf(fname, "session_%d.csv", &num) == 1) {
                    char path[40];
                    snprintf(path, sizeof(path), "/logs/session_%d.csv", num);
                    entry.close();
                    LittleFS.remove(path);
                } else {
                    entry.close();
                }
                entry = dir.openNextFile();
            }
            dir.close();
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/factory_reset - restore shipped factory config, erase logs, reboot.
    _server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (EngineData::instance().mode != SysMode::STANDBY) {
            req->send(423, "application/json",
                "{\"error\":\"Engine must be in STANDBY to perform factory reset\"}");
            return;
        }
        LittleFS.remove(Config::PATH);
        LittleFS.remove(HardwareConfig::PATH);
        if (LittleFS.exists(FACTORY_CONFIG_PATH)) {
            if (!_copyLittleFsFile(FACTORY_CONFIG_PATH, Config::PATH)) {
                req->send(500, "application/json",
                    "{\"error\":\"Failed to restore factory_config.json\"}");
                return;
            }
        }
        LittleFS.remove(FlightRecorder::PATH);
        Config::clearRuntimeStats();
        File dir = LittleFS.open("/logs");
        if (dir) {
            File entry = dir.openNextFile();
            while (entry) {
                int num = -1;
                const char* ename = entry.name();
                const char* fname = strrchr(ename, '/');
                fname = fname ? fname + 1 : ename;
                char path[40] = {};
                if (sscanf(fname, "session_%d.csv", &num) == 1)
                    snprintf(path, sizeof(path), "/logs/session_%d.csv", num);
                entry.close();
                if (path[0]) LittleFS.remove(path);
                entry = dir.openNextFile();
            }
            dir.close();
        }
        Serial.println("[WebServer] Factory reset - restored factory config, erased logs, rebooting");
        req->send(200, "application/json", "{\"ok\":true}");
        _hwRebootPending     = true;
        _hwRebootScheduledMs = millis();
    });

    // GET /api/session/list — JSON array of available run numbers, newest first
    _server.on("/api/session/list", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Collect all run numbers from /logs/session_N.csv files
        int runs[64];
        int count = 0;
        File dir = LittleFS.open("/logs");
        if (dir) {
            File entry = dir.openNextFile();
            while (entry && count < 64) {
                int num = -1;
                // entry.name() may return full path (/logs/session_1.csv) or just basename
                const char* ename = entry.name();
                const char* fname = strrchr(ename, '/');
                fname = fname ? fname + 1 : ename;
                if (sscanf(fname, "session_%d.csv", &num) == 1)
                    runs[count++] = num;
                entry = dir.openNextFile();
            }
            dir.close();
        }
        // Sort descending (simple insertion sort — at most 64 entries)
        for (int i = 1; i < count; i++) {
            int v = runs[i], j = i - 1;
            while (j >= 0 && runs[j] < v) { runs[j+1] = runs[j]; j--; }
            runs[j+1] = v;
        }
        AsyncResponseStream* resp = req->beginResponseStream("application/json");
        resp->print('[');
        for (int i = 0; i < count; i++) {
            if (i) resp->print(',');
            resp->print(runs[i]);
        }
        resp->print(']');
        req->send(resp);
    });

    // GET /api/session/log?run=N — download a specific session CSV
    // Without ?run=N serves the most recent (current) session.
    _server.on("/api/session/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        char path[40];
        if (req->hasParam("run")) {
            int run = req->getParam("run")->value().toInt();
            snprintf(path, sizeof(path), "/logs/session_%d.csv", run);
        } else {
            const char* cur = SessionLogger::currentPath();
            if (!cur || cur[0] == '\0') {
                req->send(404, "text/plain", "No session log");
                return;
            }
            strncpy(path, cur, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
        if (!LittleFS.exists(path)) {
            req->send(404, "text/plain", "Session not found");
            return;
        }
        // Extract filename from path for Content-Disposition
        const char* fname = strrchr(path, '/');
        fname = fname ? fname + 1 : path;
        char disp[64];
        snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", fname);
        AsyncWebServerResponse* resp = req->beginResponse(LittleFS, path, "text/csv");
        resp->addHeader("Content-Disposition", disp);
        req->send(resp);
    });

    // POST /update — OTA firmware upload (works over AP, no internet needed)
    // Browser sends multipart/form-data with the compiled .bin file.
    // ESP32 writes it to the inactive OTA partition and reboots.
    _server.on("/update", HTTP_POST,
        // Response callback — runs after all upload chunks received
        [](AsyncWebServerRequest* req) {
            bool ok = !_otaError && !Update.hasError();
            req->send(200, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) _otaPendingRestart = true;
            else _otaInProgress = false;
        },
        // Upload handler — called per chunk
        [](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            if (!index) {
                _otaError = false;
                // Guard: never flash firmware while the engine is running
                if (EngineData::instance().mode != SysMode::STANDBY) {
                    Serial.println("[OTA] Rejected: engine must be in STANDBY for OTA update");
                    _otaError = true;
                    return;
                }
                // Reserve the update window before evaluating outputs so a
                // queued START/tool command cannot begin between this check
                // and the first flash write.
                _otaInProgress = true;
                if (_outputsActiveForOta()) {
                    Serial.println("[OTA] Rejected: controlled output is active");
                    _otaInProgress = false;
                    _otaError = true;
                    return;
                }
                Serial.printf("[OTA] Upload start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                    _otaInProgress = false;
                    _otaError = true;
                }
            }
            if (!_otaError && EngineData::instance().mode != SysMode::STANDBY) {
                Serial.println("[OTA] Aborted: engine left STANDBY during upload");
                Update.abort();
                _otaInProgress = false;
                _otaError = true;
            }
            if (!_otaError) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                    Update.abort();
                    _otaInProgress = false;
                    _otaError = true;
                }
            }
            if (final) {
                if (!_otaError && Update.end(true)) {
                    Serial.printf("[OTA] Success: %u bytes — rebooting\n", index + len);
                } else if (!_otaError) {
                    Update.printError(Serial);
                    _otaInProgress = false;
                    _otaError = true;
                }
            }
        });

    // POST /api/web_assets - replace only compressed UI files in LittleFS.
    // This intentionally does not accept a raw LittleFS image: the filesystem
    // also contains configuration and logs that must survive a web update.
    _server.on("/api/web_assets", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            if (_assetUploadOwner != req) {
                req->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"Another web asset upload is in progress\"}");
                return;
            }
            bool ok = !_assetUploadError && _assetUploadMask == WEB_ASSET_ALL;
            if (ok) {
                for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
                    String target = _assetPath(i, false);
                    String temp = _assetPath(i, true);
                    String backup = _assetBackupPath(i);
                    if (LittleFS.exists(backup)) LittleFS.remove(backup);
                    if (LittleFS.exists(target) && !LittleFS.rename(target, backup)) {
                        ok = false;
                        break;
                    }
                    if (!LittleFS.rename(temp, target)) {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok) {
                // Restore the complete old page set if any staged swap failed.
                for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
                    String target = _assetPath(i, false);
                    String backup = _assetBackupPath(i);
                    if (LittleFS.exists(backup)) {
                        if (LittleFS.exists(target)) LittleFS.remove(target);
                        LittleFS.rename(backup, target);
                    }
                }
            } else {
                for (uint16_t i = 0; i < WEB_ASSET_COUNT; i++) {
                    String backup = _assetBackupPath(i);
                    if (LittleFS.exists(backup)) LittleFS.remove(backup);
                }
            }
            req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true,\"reboot\":true}"
                   : "{\"ok\":false,\"error\":\"Web asset update failed; upload the full asset set again\"}");
            _finishAssetUpload();
            if (ok) {
                Serial.println("[WebAssets] Update complete - rebooting");
                _hwRebootPending = true;
                _hwRebootScheduledMs = millis();
            }
        },
        [](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            if (!_assetUploadOwner) {
                _assetUploadOwner = req;
                _assetUploadError = false;
                _assetUploadMask = 0;
                _assetUploadInProgress = true;
                _assetUploadLastMs = millis();
                if (EngineData::instance().mode != SysMode::STANDBY ||
                    _otaInProgress || _outputsActiveForOta()) {
                    Serial.println("[WebAssets] Rejected: idle STANDBY required");
                    _assetUploadError = true;
                }
            }
            if (_assetUploadOwner != req || _assetUploadError) return;
            _assetUploadLastMs = millis();
            if (EngineData::instance().mode != SysMode::STANDBY || _outputsActiveForOta()) {
                Serial.println("[WebAssets] Aborted: ECU no longer idle");
                _assetUploadError = true;
                _discardAssetTemps();
                return;
            }
            const int asset = _assetIndex(filename);
            if (asset < 0 || (_assetUploadMask & (1u << asset))) {
                Serial.printf("[WebAssets] Rejected file: %s\n", filename.c_str());
                _assetUploadError = true;
                _discardAssetTemps();
                return;
            }
            if (!index) {
                String temp = _assetPath((uint16_t)asset, true);
                if (LittleFS.exists(temp)) LittleFS.remove(temp);
                _assetTempFile = LittleFS.open(temp, "w");
                if (!_assetTempFile) {
                    _assetUploadError = true;
                    _discardAssetTemps();
                    return;
                }
            }
            if (!_assetTempFile || _assetTempFile.write(data, len) != len) {
                _assetUploadError = true;
                _discardAssetTemps();
                return;
            }
            if (final) {
                _assetTempFile.close();
                _assetUploadMask |= (1u << asset);
            }
        });

    // GET /api/hardware — return the hardware section of ecu_config.json
    _server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t n = HardwareConfig::toJson(g_webTxBuf, sizeof(g_webTxBuf), true);
        if (n >= sizeof(g_webTxBuf))
            Serial.printf("[WebServer] WARNING: hardware section JSON truncated (%u >= %u)\n",
                          (unsigned)n, (unsigned)sizeof(g_webTxBuf));
        req->send(200, "application/json", g_webTxBuf);
    });

    // POST /api/hardware — validate + replace the hardware section, schedule reboot
    // Engine must be in STANDBY. Changes take effect after reboot.
    _server.on("/api/hardware", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_claimWebRx(req, index)) return;
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;   // wait for more chunks
            WebRxRelease release(req);
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }

            // Only allow hardware changes in STANDBY
            if (EngineData::instance().mode != SysMode::STANDBY) {
                req->send(423, "application/json",
                    "{\"error\":\"Engine must be in STANDBY to change hardware config\"}");
                return;
            }
            size_t previousLen = HardwareConfig::toJson(g_webTxBuf, sizeof(g_webTxBuf));
            if (previousLen >= sizeof(g_webTxBuf)) {
                req->send(500, "application/json",
                    "{\"ok\":false,\"error\":\"Current hardware section is too large to stage safely\"}");
                return;
            }
            bool ok = HardwareConfig::fromJson(g_webRxBuf, g_webRxLen);
            if (!ok) {
                req->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Invalid hardware section JSON\"}");
                return;
            }
            if (!HardwareConfig::save()) {
                HardwareConfig::fromJson(g_webTxBuf, previousLen);
                req->send(500, "application/json",
                    "{\"ok\":false,\"error\":\"Failed to write ecu_config.json hardware section\"}");
                return;
            }
            Config::sanitizeForHardware();
            if (!Config::save()) {
                HardwareConfig::fromJson(g_webTxBuf, previousLen);
                req->send(500, "application/json",
                    "{\"ok\":false,\"error\":\"Failed to synchronize settings after hardware dependency cleanup\"}");
                return;
            }
            Serial.printf("[WebServer] POST /api/hardware: saved (%u bytes) — reboot in 1s\n",
                          (unsigned)g_webRxLen);
            _hwRebootPending     = true;
            _hwRebootScheduledMs = millis();
            req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
        });

    // PATCH /api/hardware — partial update of hardware section (calibration fields only, no reboot)
    _server.on("/api/hardware", HTTP_PATCH,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            // Hardware config changes take effect immediately on the live control loop
            // (HardwareConfig static fields are read every tick).  Reject unless STANDBY.
            if (EngineData::instance().mode != SysMode::STANDBY) {
                _releaseWebRx(req);
                req->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"engine not in STANDBY\"}");
                return;
            }
            if (!_claimWebRx(req, index)) return;
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;
            WebRxRelease release(req);
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }
            JsonDocument patch;
            if (deserializeJson(patch, g_webRxBuf, g_webRxLen) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            bool calibrationOnly = true;
            for (JsonPair top : patch.as<JsonObject>()) {
                const char* topKey = top.key().c_str();
                if (strcmp(topKey, "sensors") == 0 && top.value().is<JsonObject>()) {
                    for (JsonPair sensor : top.value().as<JsonObject>()) {
                        const char* sensorKey = sensor.key().c_str();
                        if (!sensor.value().is<JsonObject>()) { calibrationOnly = false; break; }
                        for (JsonPair field : sensor.value().as<JsonObject>()) {
                            const char* fieldKey = field.key().c_str();
                            bool allowed = (strcmp(sensorKey, "oil_temp") == 0 &&
                                            (strcmp(fieldKey, "ntc_beta") == 0 ||
                                             strcmp(fieldKey, "ntc_r0") == 0 ||
                                             strcmp(fieldKey, "ntc_r_fixed") == 0))
                                        || (strcmp(sensorKey, "batt_voltage") == 0 &&
                                            strcmp(fieldKey, "divider") == 0)
                                        || (strcmp(sensorKey, "torque") == 0 &&
                                            (strcmp(fieldKey, "scale") == 0 ||
                                             strcmp(fieldKey, "offset") == 0));
                            if (!allowed) { calibrationOnly = false; break; }
                        }
                        if (!calibrationOnly) break;
                    }
                } else if (strcmp(topKey, "actuators") == 0 && top.value().is<JsonObject>()) {
                    for (JsonPair actuator : top.value().as<JsonObject>()) {
                        const char* actuatorKey = actuator.key().c_str();
                        bool validActuator = strcmp(actuatorKey, "oil_pump") == 0 ||
                                             strcmp(actuatorKey, "glow_plug") == 0 ||
                                             strcmp(actuatorKey, "igniter") == 0 ||
                                             strcmp(actuatorKey, "igniter2") == 0;
                        if (!validActuator || !actuator.value().is<JsonObject>()) {
                            calibrationOnly = false;
                            break;
                        }
                        for (JsonPair field : actuator.value().as<JsonObject>()) {
                            const char* fieldKey = field.key().c_str();
                            if (strcmp(fieldKey, "current_zero_v") != 0 &&
                                strcmp(fieldKey, "current_mv_a") != 0) {
                                calibrationOnly = false;
                                break;
                            }
                        }
                        if (!calibrationOnly) break;
                    }
                } else {
                    calibrationOnly = false;
                }
                if (!calibrationOnly) break;
            }
            if (!calibrationOnly) {
                req->send(400, "application/json",
                    "{\"error\":\"hardware PATCH accepts calibration fields only; use Hardware Save for topology changes\"}");
                return;
            }
            // Merge patch into a full hardware document and re-apply
            size_t hwLen = HardwareConfig::toJson(g_webTxBuf, sizeof(g_webTxBuf));
            if (hwLen >= sizeof(g_webTxBuf)) {
                req->send(500, "application/json", "{\"error\":\"hardware config too large for merge buffer\"}");
                return;
            }
            JsonDocument current;
            if (deserializeJson(current, g_webTxBuf) != DeserializationError::Ok) {
                req->send(500, "application/json", "{\"error\":\"failed to read current hardware config\"}");
                return;
            }
            JsonDocument previous;
            previous.set(current);
            _mergeJsonObject(current.as<JsonObject>(), patch.as<JsonObjectConst>());
            size_t merged = serializeJson(current, g_webTxBuf, sizeof(g_webTxBuf));
            if (merged >= sizeof(g_webTxBuf)) {
                // Buffer was too small — output is truncated; reject rather than corrupt config
                req->send(500, "application/json", "{\"error\":\"merged hardware config too large\"}");
                return;
            }
            if (!HardwareConfig::fromJson(g_webTxBuf, merged)) {
                req->send(400, "application/json", "{\"error\":\"hardware patch rejected\"}");
                return;
            }
            if (!HardwareConfig::save()) {
                size_t previousLen = serializeJson(previous, g_webTxBuf, sizeof(g_webTxBuf));
                if (previousLen < sizeof(g_webTxBuf))
                    HardwareConfig::fromJson(g_webTxBuf, previousLen);
                req->send(500, "application/json", "{\"error\":\"failed to write hardware config\"}");
                return;
            }
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // GET /api/ecu_config — download full unified config (hardware + settings)
    _server.on("/api/ecu_config", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* resp = req->beginResponse(
            LittleFS, Config::PATH, "application/json");
        resp->addHeader("Content-Disposition", "attachment; filename=\"ecu_config.json\"");
        req->send(resp);
    });

    // POST /api/ecu_config — upload full unified config, apply all sections, reboot
    _server.on("/api/ecu_config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                if (_configRestoreOwner) {
                    req->send(409, "application/json",
                        "{\"error\":\"Another full configuration restore is in progress\"}");
                    return;
                }
                _configRestoreOwner = req;
                _configRestoreError = total > 65536;
                _configRestoreLastMs = millis();
                LittleFS.remove("/ecu_config.restore.tmp");
                if (!_configRestoreError) {
                    _configRestoreFile = LittleFS.open("/ecu_config.restore.tmp", "w");
                    _configRestoreError = !_configRestoreFile;
                }
            }
            if (_configRestoreOwner != req) return;
            _configRestoreLastMs = millis();
            if (!_configRestoreError && _configRestoreFile.write(data, len) != len)
                _configRestoreError = true;
            if (index + len < total) return;
            if (_configRestoreFile) _configRestoreFile.close();
            if (_configRestoreError) {
                req->send(400, "application/json", "{\"error\":\"configuration file is too large or could not be staged\"}");
                _finishConfigRestore();
                return;
            }

            if (EngineData::instance().mode != SysMode::STANDBY) {
                req->send(423, "application/json",
                    "{\"error\":\"Engine must be in STANDBY to upload config\"}");
                _finishConfigRestore();
                return;
            }
            JsonDocument fullDoc;
            File staged = LittleFS.open("/ecu_config.restore.tmp", "r");
            DeserializationError err = staged
                ? deserializeJson(fullDoc, staged) : DeserializationError::EmptyInput;
            if (staged) staged.close();
            if (err != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                _finishConfigRestore();
                return;
            }
            if (!fullDoc[HardwareConfig::SECTION].is<JsonObject>()
                || !fullDoc[Config::SECTION].is<JsonObject>()) {
                req->send(400, "application/json",
                    "{\"error\":\"full ECU config must contain hardware and settings sections\"}");
                _finishConfigRestore();
                return;
            }

            JsonDocument hwDoc;
            hwDoc.set(fullDoc[HardwareConfig::SECTION]);
            if (strcmp(hwDoc["wifi_password"] | "", "__KEEP_PASSWORD__") == 0) {
                hwDoc["wifi_password"] = HardwareConfig::wifiPassword;
                fullDoc[HardwareConfig::SECTION].set(hwDoc);
            }
            if (!HardwareConfig::validateJson(hwDoc)) {
                req->send(400, "application/json", "{\"error\":\"hardware section rejected\"}");
                _finishConfigRestore();
                return;
            }

            JsonDocument settingsDoc;
            settingsDoc.set(fullDoc[Config::SECTION]);
            if (!Config::validateJson(settingsDoc)) {
                req->send(400, "application/json", "{\"error\":\"settings section rejected\"}");
                _finishConfigRestore();
                return;
            }
            if (strcmp(hwDoc["profile_id"] | "", settingsDoc["profile_id"] | "") != 0) {
                req->send(400, "application/json",
                    "{\"error\":\"hardware and settings profile_id must identify the same engine\"}");
                _finishConfigRestore();
                return;
            }

            size_t previousHwLen = HardwareConfig::toJson(g_webTxBuf, sizeof(g_webTxBuf));
            JsonDocument previousSettings;
            Config::toJson(previousSettings);
            size_t stagedHwLen = serializeJson(hwDoc, g_webRxBuf, sizeof(g_webRxBuf));
            if (previousHwLen >= sizeof(g_webTxBuf) ||
                stagedHwLen >= sizeof(g_webRxBuf) ||
                !HardwareConfig::fromJson(g_webRxBuf, stagedHwLen) ||
                !Config::fromJson(settingsDoc)) {
                if (previousHwLen < sizeof(g_webTxBuf)) HardwareConfig::fromJson(g_webTxBuf, previousHwLen);
                Config::fromJson(previousSettings);
                req->send(400, "application/json", "{\"error\":\"config dependency cleanup rejected uploaded sections\"}");
                _finishConfigRestore();
                return;
            }
            Config::sanitizeForHardware();
            JsonDocument sanitizedHw;
            JsonDocument sanitizedSettings;
            size_t sanitizedHwLen = HardwareConfig::toJson(g_webRxBuf, sizeof(g_webRxBuf), false);
            if (sanitizedHwLen >= sizeof(g_webRxBuf) ||
                deserializeJson(sanitizedHw, g_webRxBuf, sanitizedHwLen) != DeserializationError::Ok) {
                HardwareConfig::fromJson(g_webTxBuf, previousHwLen);
                Config::fromJson(previousSettings);
                req->send(500, "application/json", "{\"error\":\"sanitized hardware section too large\"}");
                _finishConfigRestore();
                return;
            }
            Config::toJson(sanitizedSettings);
            fullDoc[HardwareConfig::SECTION].set(sanitizedHw);
            fullDoc[Config::SECTION].set(sanitizedSettings);
            HardwareConfig::fromJson(g_webTxBuf, previousHwLen);
            Config::fromJson(previousSettings);

            // Store one complete engine file only after both sections validate.
            // Runtime values are loaded from this committed file after reboot.
            if (!_writeUnifiedConfigAtomically(fullDoc)) {
                req->send(500, "application/json", "{\"error\":\"failed to atomically save ecu_config.json\"}");
                _finishConfigRestore();
                return;
            }

            Serial.printf("[WebServer] POST /api/ecu_config: %u bytes — reboot in 1s\n", (unsigned)total);
            _finishConfigRestore(false);
            _hwRebootPending     = true;
            _hwRebootScheduledMs = millis();
            req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
        });

    // 404
    _server.onNotFound([isCaptive](AsyncWebServerRequest* req) {
        if (isCaptive(req)) {
            String target = "http://";
            target += WiFi.softAPIP().toString();
            target += "/";
            req->redirect(target);
            return;
        }
        req->send(404);
    });

    // WebSocket — client-pull model with PING/PONG rescue.
    //
    // Core problem: async_tcp (AsyncTCP 3.x + IDF5) intermittently blocks for
    // 2–20 s waiting for events, even when "p" messages are arriving every 500 ms.
    // This is the same root cause that required CONFIG_ASYNC_TCP_USE_WDT=0.
    //
    // Primary path: JS sends "p" periodically and WS_EVT_DATA replies inside
    // async_tcp. WebSocket messages are live-data frames only; each page loads
    // one full snapshot from /api/data during boot for limits and labels. This
    // prevents large full frames growing the async TCP telemetry allocation.
    //
    // Rescue path: if canSend() was false when "p" arrived (previous frame still
    // in-flight), _wsPendingResponse is set.  tick() notices and calls pingAll()
    // every 200 ms — a tiny PING that crosses the task boundary cheaply.  The
    // client auto-replies with PONG, which fires WS_EVT_PONG inside async_tcp,
    // where canSend() will be true and the pending frame is delivered.
    _ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type,
                   void*, uint8_t*, size_t) {
        bool shouldSend = false;
        bool full       = false;

        if (type == WS_EVT_CONNECT) {
            shouldSend = true;
            full       = false;  // /api/data supplies the full boot snapshot
        } else if (type == WS_EVT_DATA) {
            if (!client->canSend()) {
                _wsPendingResponse = true;
                return;
            }
            // A new pull from the browser is already proof that the socket is
            // responsive again; resume immediately instead of waiting for ping.
            _wsPendingResponse = false;
            _wsPendingFull = false;
            // A fresh pull is also the fastest recovery from a deferred frame.
            full = false;
            _wsPendingFull     = false;
            _wsPendingResponse = true;
            shouldSend = true;
        } else if (type == WS_EVT_PONG && _wsPendingResponse) {
            // Rescue: tick() sent a PING because canSend() was false; now we are
            // back inside async_tcp context and the pipe should be clear.
            shouldSend = true;
            full       = false;
        }

        if (!shouldSend || !client || !client->canSend()) return;
        _wsPendingResponse = false;
        // Keep each WebSocket frame small. ESPAsyncWebServer copies outgoing text
        // into a heap-backed vector; the previous full-frame size could exhaust
        // ESP32 heap and throw from operator new in the async TCP task.
        static char buf[3584];
        static JsonDocument doc;
        size_t n = _buildTelemetry(buf, sizeof(buf), doc, full);
        if (n < sizeof(buf)) {
            if (!_sendTelemetryFrame(client, buf, n)) _wsPendingResponse = true;
        } else if (full) {
            doc.clear();
            n = _buildTelemetry(buf, sizeof(buf), doc, false);
            if (n >= sizeof(buf) || !_sendTelemetryFrame(client, buf, n))
                _wsPendingResponse = true;
        }
    });
    _server.addHandler(&_ws);
}

// ── Public API ────────────────────────────────────────────────
void WebServer::begin() {
    _recoverInterruptedAssetUpdate();
    _startWiFi();
    _setupRoutes();
    _server.begin();
    Serial.println("[WebServer] Started on port 80");
}

void WebServer::tick() {
    unsigned long _t0 = millis();
    _dns.processNextRequest();
    unsigned long _t1 = millis();
    FlightRecorder::runEviction();
    unsigned long _t2 = millis();
    SessionLogger::drainQueue();
    unsigned long _t3 = millis();
    Config::flushPendingSave();
    unsigned long _t4 = millis();
    Config::flushPendingRuntimeStats();
    unsigned long _t5 = millis();
    if (_t5 - _t0 > 200) {
        Serial.printf("[tick] SLOW %lums: dns=%lu evict=%lu drain=%lu save=%lu stats=%lu\n",
            _t5-_t0, _t1-_t0, _t2-_t1, _t3-_t2, _t4-_t3, _t5-_t4);
    }

    // ── LittleFS stats cache ──────────────────────────────────
    // Refresh every 10 s from webTask so _buildTelemetry never has to call
    // usedBytes() from inside the async_tcp task context (avoids FS mutex
    // contention / priority inversion with SessionLogger writes).
    {
        static unsigned long _fsStatMs = 0;
        unsigned long now = millis();
        if (now - _fsStatMs >= 10000) {
            _fsStatMs  = now;
            s_fsTotal  = LittleFS.totalBytes() / 1024;
            s_fsUsed   = LittleFS.usedBytes()  / 1024;
        }
    }

    // ── PING rescue ───────────────────────────────────────────
    // If a "p" pull arrived while canSend() was false, _wsPendingResponse is
    // set.  Send a tiny WS PING every 200 ms; the client auto-replies with
    // PONG, which fires WS_EVT_PONG inside async_tcp — the correct context to
    // deliver the pending telemetry frame without a cross-task handoff.
    if (_wsPendingResponse && _ws.count() > 0) {
        unsigned long now = millis();
        if (now - _wsPingMs >= 1000) {
            _wsPingMs = now;
            _ws.pingAll();
        }
    }

    // OTA: reboot after response has been sent
    if (_otaPendingRestart) {
        delay(200);
        ESP.restart();
    }
    if (_assetUploadInProgress && (millis() - _assetUploadLastMs) > 30000) {
        Serial.println("[WebAssets] Timed out - discarding staged upload");
        _assetUploadError = true;
        _finishAssetUpload();
    }
    if (_configRestoreOwner && (millis() - _configRestoreLastMs) > 30000) {
        Serial.println("[Config] Timed out - discarding staged full restore");
        _finishConfigRestore();
    }
    // Hardware config: reboot 1 second after POST /api/hardware saves successfully
    if (_hwRebootPending && (millis() - _hwRebootScheduledMs) >= 1000) {
        Serial.println("[WebServer] Hardware config reboot");
        delay(100);
        ESP.restart();
    }
    // Purge stale WebSocket clients every 2 s (handles page navigations that leave
    // ghost connections).  Keep at most 1 — multiple stale connections cause
    // canSend() to return false and silently drop the live client's frames.
    if (WiFi.softAPgetStationNum() == 0) return;
    unsigned long now = millis();
    static unsigned long _lastCleanMs = 0;
    if (now - _lastCleanMs >= 2000) {
        _lastCleanMs = now;
        _ws.cleanupClients(1);
    }
}

bool WebServer::otaInProgress() {
    return _maintenanceUploadInProgress();
}
