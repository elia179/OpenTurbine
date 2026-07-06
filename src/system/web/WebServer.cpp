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
extern AnalogThSensor     g_sensorAbFlame;
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
static AsyncWebServerRequest* _otaUploadOwner = nullptr;
static unsigned long _otaUploadLastMs        = 0;
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
static const char*   _pendingRestartReason   = nullptr;

// ── WebSocket telemetry state ─────────────────────────────────
// _wsPendingResponse: set when a "p" arrived but canSend() was false.
// tick() detects this and sends a WS PING; the PONG fires WS_EVT_PONG
// inside the async_tcp task (correct context), which then delivers the
// queued telemetry frame without waiting for another "p" from the client.
static volatile bool _wsPendingResponse = false;
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
// makes the total DRAM footprint explicit: 2 × 16384 = 32 KB instead of the ~68 KB that
// nine separate function-local statics would consume.
static char   g_webRxBuf[16384];  // request body accumulation (POST / PATCH)
static size_t g_webRxLen     = 0;
static bool   g_webRxOverflow = false;
static char   g_webTxBuf[16384];  // response serialisation + PATCH merge work buffer
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
    "tools.html.gz", "theme.js.gz"
};
static constexpr uint16_t WEB_ASSET_COUNT = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]);
static constexpr uint16_t WEB_ASSET_ALL = (1u << WEB_ASSET_COUNT) - 1u;

static bool _maintenanceUploadInProgress() {
    return _otaInProgress || _assetUploadInProgress || (_configRestoreOwner != nullptr);
}

// FAULT is the boot-time config-integrity state (profile mismatch / config load
// failure): a light lockout where only START is blocked. Every other STANDBY
// gate treats FAULT as standby-like so the user can repair the ECU — mirrors
// handleCommand()'s standbyLike in main.cpp.
static bool _isStandbyLike(SysMode mode) {
    return mode == SysMode::STANDBY || mode == SysMode::FAULT;
}

static bool _isStandbyToolCommand(OTCommand cmd) {
    switch (cmd) {
        case OTCommand::FUEL_PRIME:
        case OTCommand::OIL_PRIME:
        case OTCommand::IGN_TEST:
        case OTCommand::IGN2_TEST:
        case OTCommand::START_TEST:
        case OTCommand::FUEL_SOL_TEST:
        case OTCommand::IDLE_TEST:
        case OTCommand::SET_OIL_DEMAND:
        case OTCommand::SET_OIL_PCT:
        case OTCommand::EXTRA_COOLDOWN:
        case OTCommand::CLEAR_LOG:
        case OTCommand::OIL_SCAV_TEST:
        case OTCommand::COOL_FAN_TEST:
        case OTCommand::AIRSTARTER_TEST:
        case OTCommand::BLEED_VALVE_TEST:
        case OTCommand::GLOW_TEST:
        case OTCommand::FUEL_PUMP2_TEST:
        case OTCommand::AB_SOL_TEST:
        case OTCommand::AB_PUMP_TEST:
        case OTCommand::STARTER_EN_TEST:
        case OTCommand::PROP_PITCH_TEST:
            return true;
        default:
            return false;
    }
}

static bool _startsTimedActuatorTest(const OTPacket& pkt) {
    switch (pkt.cmd) {
        case OTCommand::FUEL_PRIME:
        case OTCommand::OIL_PRIME:
        case OTCommand::IGN_TEST:
        case OTCommand::IGN2_TEST:
        case OTCommand::START_TEST:
        case OTCommand::FUEL_SOL_TEST:
        case OTCommand::IDLE_TEST:
        case OTCommand::OIL_SCAV_TEST:
        case OTCommand::COOL_FAN_TEST:
        case OTCommand::AIRSTARTER_TEST:
        case OTCommand::BLEED_VALVE_TEST:
        case OTCommand::GLOW_TEST:
        case OTCommand::FUEL_PUMP2_TEST:
        case OTCommand::AB_SOL_TEST:
        case OTCommand::AB_PUMP_TEST:
        case OTCommand::STARTER_EN_TEST:
        case OTCommand::PROP_PITCH_TEST:
            return true;
        case OTCommand::EXTRA_COOLDOWN:
            return pkt.iParam > 0;
        default:
            return false;
    }
}

static const char* _missingHardwareForCommand(OTCommand cmd) {
    switch (cmd) {
        case OTCommand::FUEL_PRIME:
        case OTCommand::FUEL_SOL_TEST: return HardwareConfig::hasFuelSol ? nullptr : "Fuel solenoid is not configured";
        case OTCommand::OIL_PRIME:
        case OTCommand::SET_OIL_PCT:
        case OTCommand::SET_OIL_DEMAND: return HardwareConfig::hasOilPump ? nullptr : "Oil pump is not configured";
        case OTCommand::IGN_TEST: return HardwareConfig::hasIgniter ? nullptr : "Igniter 1 is not configured";
        case OTCommand::IGN2_TEST: return HardwareConfig::hasIgniter2 ? nullptr : "Igniter 2 is not configured";
        case OTCommand::START_TEST: return HardwareConfig::hasStarter ? nullptr : "Starter is not configured";
        case OTCommand::IDLE_TEST: return HardwareConfig::hasThrottle ? nullptr : "Throttle output is not configured";
        case OTCommand::OIL_SCAV_TEST: return HardwareConfig::hasOilScavengePump ? nullptr : "Oil scavenge pump is not configured";
        case OTCommand::COOL_FAN_TEST: return HardwareConfig::hasCoolFan ? nullptr : "Cooling fan is not configured";
        case OTCommand::AIRSTARTER_TEST: return HardwareConfig::hasAirstarterSol ? nullptr : "Airstarter solenoid is not configured";
        case OTCommand::BLEED_VALVE_TEST: return HardwareConfig::hasBleedValve ? nullptr : "Bleed valve is not configured";
        case OTCommand::GLOW_TEST: return HardwareConfig::hasGlowPlug ? nullptr : "Glow plug is not configured";
        case OTCommand::FUEL_PUMP2_TEST: return HardwareConfig::hasFuelPump2 ? nullptr : "Fuel pump 2 is not configured";
        case OTCommand::AB_SOL_TEST:
            return (HardwareConfig::hasAfterburner && HardwareConfig::hasAbSol) ? nullptr : "Afterburner solenoid is not configured";
        case OTCommand::AB_PUMP_TEST:
            return (HardwareConfig::hasAfterburner && HardwareConfig::hasAbPump) ? nullptr : "Afterburner pump is not configured";
        case OTCommand::STARTER_EN_TEST: return HardwareConfig::hasStarterEn ? nullptr : "Starter enable relay is not configured";
        case OTCommand::PROP_PITCH_TEST: return HardwareConfig::hasPropPitch ? nullptr : "Prop pitch actuator is not configured";
        case OTCommand::TOGGLE_DYNAMIC_IDLE:
            return HardwareConfig::hasDynamicIdle ? nullptr : "Dynamic Idle is not enabled in hardware";
        case OTCommand::TOGGLE_LIMP_MODE:
            return HardwareConfig::hasThrottle ? nullptr : "Limp Mode requires a throttle output";
        case OTCommand::STARTER_ASSIST:
            return (HardwareConfig::hasStarter && HardwareConfig::starterAssistEnabled && HardwareConfig::hasN1Rpm) ? nullptr : "Starter assist requires starter output and N1 RPM feedback";
        case OTCommand::AB_FIRE:
        case OTCommand::AB_STOP:
            return HardwareConfig::hasAfterburner ? nullptr : "Afterburner is not configured";
        default:
            return nullptr;
    }
}

static const char* _commandPreflightRejectReason(const OTPacket& pkt) {
    const auto& ed = EngineData::instance();
    // tick() will call ESP.restart() unconditionally once the window elapses —
    // never begin a new actuator action that a reboot would interrupt mid-stream.
    // AB_STOP stays allowed: it only de-energizes outputs.
    if (_hwRebootPending && pkt.cmd != OTCommand::AB_STOP) {
        return "ECU is rebooting to apply a saved configuration. Reconnect and retry.";
    }
    if (const char* hw = _missingHardwareForCommand(pkt.cmd)) return hw;
    if (_isStandbyToolCommand(pkt.cmd) && !_isStandbyLike(ed.mode)) {
        return "Command is only available in STANDBY";
    }
    if (_startsTimedActuatorTest(pkt) && _outputsActiveForOta()) {
        return "Another actuator output is already active";
    }
    if (pkt.cmd == OTCommand::EXTRA_COOLDOWN && pkt.iParam > 0) {
        const bool ecUseStarter = HardwareConfig::hasStarter && Config::cooldownUseStarter;
        const bool ecUseOil = HardwareConfig::hasOilPump && Config::cooldownUseOilPump;
        const bool ecUseScavenge = HardwareConfig::hasOilScavengePump && Config::cooldownUseScavengePump;
        if (!ecUseStarter && !ecUseOil && !ecUseScavenge) {
            return "No fitted cooldown actuator is enabled";
        }
    }
    if (pkt.cmd == OTCommand::TOGGLE_DEV_MODE && !_isStandbyLike(ed.mode)) {
        return "Developer Mode can only be changed in STANDBY";
    }
    if (pkt.cmd == OTCommand::TOGGLE_BENCH_MODE) {
        if (!_isStandbyLike(ed.mode)) return "Bench Mode can only be changed in STANDBY";
        if (!ed.devMode) return "Enable Developer Mode before Bench Mode";
    }
    if (pkt.cmd == OTCommand::TOGGLE_SAFETY_CHECKS) {
        if (!_isStandbyLike(ed.mode)) return "Safety bypass can only be changed in STANDBY";
        if (!ed.devMode || !ed.benchMode) return "Enable Developer Mode and Bench Mode before safety bypass";
    }
    if (pkt.cmd == OTCommand::STARTER_ASSIST && pkt.iParam != 0 && ed.mode != SysMode::RUNNING) {
        return "Starter assist can only be enabled while RUNNING";
    }
    if ((pkt.cmd == OTCommand::TOGGLE_DYNAMIC_IDLE || pkt.cmd == OTCommand::TOGGLE_LIMP_MODE)
        && !(_isStandbyLike(ed.mode) || ed.mode == SysMode::RUNNING)) {
        return "Command is only available in STANDBY or RUNNING";
    }
    if (pkt.cmd == OTCommand::AB_FIRE) {
        if (ed.mode != SysMode::RUNNING) return "Afterburner can only be fired while RUNNING";
        if (HardwareConfig::abTriggerSource != 0) {
            return "Manual FIRE is only available when AB trigger source is Manual command only";
        }
        if (!HardwareConfig::hasAbSol && !HardwareConfig::hasAbPump) {
            return "Afterburner fuel output is not configured";
        }
        if (!(ed.abMode == ABMode::Off || ed.abMode == ABMode::Fault)) {
            return "Afterburner is already active or shutting down";
        }
    }
    return nullptr;
}

static bool _outputActiveBlocksStart() {
    const auto& ed = EngineData::instance();
    const bool standbyOilOnly = ed.standbyOilFeedActive &&
                                ed.oilPumpPct > 0.01f &&
                                ed.oilPumpPct <= (Config::standbyOilFeedPct + 0.5f);
    return ed.throttleDemand > 0.001f || ed.fuelPump2Demand > 0.001f ||
           (ed.oilPumpPct > 0.01f && !standbyOilOnly) ||
           ed.starterDemand > 0.001f || ed.abPumpDemand > 0.001f ||
           ed.propPitchDemand > 0.001f || ed.glowPlugDemand > 0.001f ||
           ed.fuelSolOpen || ed.igniterOn || ed.igniter2On ||
           ed.starterEnabled || ed.coolFanOn || ed.airstarterOpen ||
           ed.oilScavengeOn || ed.bleedValveOpen || ed.abSolOpen;
}

static bool _startInhibitActive() {
    const auto& ed = EngineData::instance();
    auto& hw = HardwareConfig::instance();
    for (int i = 0; i < HardwareConfig::MAX_DI; i++) {
        if (hw.diCh[i].pin >= 0 &&
            strcmp(hw.diCh[i].role, "inhibit_start") == 0 &&
            ed.diState[i]) {
            return true;
        }
    }
    return false;
}

static const char* _startPreflightRejectReason() {
    const auto& ed = EngineData::instance();
    // A reboot scheduled by hardware save / factory reset / config restore fires
    // unconditionally in tick() — starting now would reboot mid-startup with the
    // fuel solenoid and igniter energized.
    if (_hwRebootPending) {
        return "ECU is rebooting to apply a saved configuration. Reconnect and retry.";
    }
    if (ed.mode == SysMode::FAULT) {
        return "ECU is in FAULT mode: hardware config or profile ID failed boot validation. "
               "Everything except START still works - fix and save the configuration to reboot into STANDBY.";
    }
    if (ed.mode != SysMode::STANDBY) {
        return "Engine is not in STANDBY";
    }
    if (!Config::profileMatch || ed.configLocked) {
        return "Configuration is locked or profile ID does not match";
    }
    if (ed.stopSwitchActive) {
        return "STOP switch is active. Release STOP before pressing START.";
    }
    if (_startInhibitActive()) {
        return "Start inhibit digital input is active";
    }
    if (ed.extraCooldownActive) {
        return "Extra Cooldown is running. Stop it on the Tools page or wait for it to finish.";
    }
    if (_outputActiveBlocksStart()) {
        return "An actuator test or prime output is still active. Wait for Tools actions to finish.";
    }
    if (ed.seqHasStructuralErrors) {
        return "Startup sequence contains unknown or unavailable block names. Open Sequence, fix red errors, and save.";
    }
    if (ed.seqHasErrors && !ed.benchMode) {
        return "Startup sequence requires hardware that is not configured. Check Sequence, or enable Bench Mode for dry testing.";
    }
    return nullptr;
}

static void _sendCommandReject(AsyncWebServerRequest* req, int status, const char* reason) {
    snprintf(g_webTxBuf, sizeof(g_webTxBuf),
             "{\"ok\":false,\"error\":\"%s\"}", reason ? reason : "Command rejected");
    req->send(status, "application/json", g_webTxBuf);
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
        LittleFS.remove(TMP_PATH);
        LittleFS.remove(BAK_PATH);
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

// Serializes maintenance-upload state (Update handle, _assetTempFile,
// _configRestoreFile and their owner/flag variables) between the async_tcp
// upload handlers and the webTask tick() idle-timeout cleanup.  Without it a
// chunk arriving exactly at the 30 s timeout boundary can write a File object
// that tick() is concurrently closing.  Statically allocated in begin().
static StaticSemaphore_t _uploadMuxBuf;
static SemaphoreHandle_t _uploadMux = nullptr;

class UploadLock {
public:
    UploadLock()  { if (_uploadMux) xSemaphoreTake(_uploadMux, portMAX_DELAY); }
    ~UploadLock() { if (_uploadMux) xSemaphoreGive(_uploadMux); }
};

static AsyncWebServer  _server(80);
static AsyncWebSocket  _ws("/ws");
static DNSServer       _dns;                 // captive portal DNS

static void _sendGzipAsset(AsyncWebServerRequest* req, const char* path,
                           const char* contentType, const char* cacheControl) {
    if (!LittleFS.exists(path)) {
        AsyncWebServerResponse* resp = req->beginResponse(
            503, "text/plain", "Web UI asset missing - re-upload web assets or reflash filesystem");
        resp->addHeader("Cache-Control", "no-store");
        resp->addHeader("Connection", "close");
        req->send(resp);
        return;
    }
    AsyncWebServerResponse* resp = req->beginResponse(LittleFS, path, contentType);
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control", cacheControl);
    resp->addHeader("Connection", "close");
    req->send(resp);
}

static constexpr const char* SHARED_ASSET_CACHE = "no-cache";

static void _finalizeJsonResponse(AsyncWebServerResponse* resp) {
    if (!resp) return;
    resp->addHeader("Cache-Control", "no-store");
    resp->addHeader("Connection", "close");
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
static void _startWiFi() {
    // Software restart after a hardware save can leave the ESP WiFi driver and
    // client devices with stale AP/TCP state.  Force a clean radio/DNS/mDNS
    // bring-up so save-and-reboot behaves like a cold power cycle.
    _dns.stop();
    MDNS.end();
    WiFi.persistent(false);
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(150);

    WiFi.mode(WIFI_AP);
    const IPAddress apIP(192, 168, 4, 1);
    const IPAddress apGateway(192, 168, 4, 1);
    const IPAddress apSubnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    const char* ssidFull = HardwareConfig::profileId[0] ? HardwareConfig::profileId : "OpenTurbine";
    // IEEE 802.11 SSID max is 32 bytes — clamp an over-long profile_id at
    // use only (the stored profile_id keeps its full value; the Hardware
    // page warns above 32 bytes but never blocks the save).
    char ssid[33];
    strncpy(ssid, ssidFull, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    if (strlen(ssidFull) > 32) {
        // don't end on a UTF-8 character split by the byte clamp
        int i = 31;
        while (i > 0 && ((unsigned char)ssid[i] & 0xC0) == 0x80) i--;
        unsigned char lead = (unsigned char)ssid[i];
        int expect = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : (lead >= 0xC0 ? 2 : 1));
        if (i + expect > 32) ssid[i] = '\0';
    }
    const char* pwd  = HardwareConfig::wifiPassword[0] ? HardwareConfig::wifiPassword : nullptr;
    bool apOk = WiFi.softAP(ssid, pwd);  // SSID = hardware profile_id; password optional
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
    IPAddress activeIp = WiFi.softAPIP();
    Serial.printf("[WiFi] AP: %s  IP: %s  %s  TX=%d dBm %s\n", ssid, activeIp.toString().c_str(),
                  pwd ? "(password protected)" : "(open network)",
                  (int)HardwareConfig::wifiTxPowerDbm,
                  apOk ? "" : "(softAP start reported failure)");

    // Captive portal DNS — answers all DNS queries with our IP so phones
    // open the dashboard automatically when joining the AP.
    _dns.start(53, "*", activeIp);
    Serial.println("[WiFi] Captive portal DNS started");

    // mDNS — accessible as http://ot.local on any mDNS-capable client
    if (MDNS.begin("ot")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[WiFi] mDNS: http://ot.local");
    }
}

static void _scheduleRestart(const char* reason, uint32_t delayMs = 5000) {
    _pendingRestartReason = reason;
    _hwRebootPending = true;
    _hwRebootScheduledMs = millis() + delayMs;
}

static void _restartCleanly(const char* reason) {
    Serial.printf("[WebServer] Restarting: %s\n", reason ? reason : "requested");
    // Do not explicitly tear down the AP before ESP.restart().  Windows treats
    // a deliberate AP disappearance as a reason to roam away to another known
    // network, which makes 192.168.4.1 and captive DNS look dead after reboot.
    // Give the HTTP response time to flush, then let reset drop/recreate WiFi.
    delay(250);
    ESP.restart();
}

// ── Telemetry JSON builder ────────────────────────────────────
// full=true  → complete frame: all fields (served by /api/data REST)
// full=false → fast frame: only real-time sensor/state fields (WebSocket)
//
// The JS keeps the last received value for every field, so omitting slow
// fields on fast frames has no visible effect after the first full frame.
static size_t _buildTelemetry(char* buf, size_t len, JsonDocument& doc, bool full) {
    auto& ed = EngineData::instance();
    doc.clear();
    const float p1Bar = ed.p1;
    const float p2Bar = ed.p2;
    const float maxP1Bar = ed.maxP1;
    const float maxP2Bar = ed.maxP2;

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
    doc["p1"]                    = (float)(int)(std::max(0.0f, p1Bar) * 100) / 100.0f;
    doc["p2"]                    = (float)(int)(std::max(0.0f, p2Bar) * 100) / 100.0f;
    doc["p1_raw"]                = g_sensorP1.rawCounts();
    doc["p2_raw"]                = g_sensorP2.rawCounts();
    doc["p1_healthy"]            = ed.p1Healthy;
    doc["p2_healthy"]            = ed.p2Healthy;
    doc["flame_healthy"]         = ed.flameHealthy;
    doc["max_p1"]                = (float)(int)(std::max(0.0f, maxP1Bar) * 100) / 100.0f;
    doc["max_p2"]                = (float)(int)(std::max(0.0f, maxP2Bar) * 100) / 100.0f;
    float fuelPressBar           = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_press"]            = fuelPressBar;
    doc["fuel_press_raw"]        = ed.fuelPressRaw;
    doc["fuel_press_healthy"]    = ed.fuelPressHealthy;
    doc["max_fuel_press"]        = (float)(int)(ed.maxFuelPressure * 100) / 100.0f;
    doc["fuel_flow_healthy"]     = ed.fuelFlowHealthy;
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
    doc["starter_demand"]        = (float)(int)(ed.starterDemand * 1000) / 1000.0f;
    doc["starter_enabled"]       = ed.starterEnabled;
    doc["fuel_sol_open"]         = ed.fuelSolOpen;
    doc["igniter_on"]            = ed.igniterOn;
    doc["igniter2_on"]           = ed.igniter2On;
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
    doc["boot_count"]            = ed.bootCount;
    doc["loop_counter"]          = ed.loopCounter;
    doc["loop_hz"]               = ed.loopHz;
    doc["loop_period_ms"]        = ed.loopPeriodMs;
    doc["loop_exec_avg_ms"]      = ed.loopExecAvgMs;
    doc["loop_exec_max_ms"]      = ed.loopExecMaxMs;
    doc["loop_sensors_ms"]       = ed.loopSensorsMs;
    doc["loop_sequencer_ms"]     = ed.loopSequencerMs;
    doc["loop_controllers_ms"]   = ed.loopControllersMs;
    doc["loop_actuators_ms"]     = ed.loopActuatorsMs;
    doc["loop_logging_ms"]       = ed.loopLoggingMs;
    doc["loop_led_ms"]           = ed.loopLedMs;
    doc["session_dropped_rows"]  = SessionLogger::droppedRows();
    doc["flight_dropped_events"] = FlightRecorder::droppedEvents();
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
    doc["ab_trigger_source"]     = HardwareConfig::abTriggerSource;
    doc["ab_arm_switch_on"]      = ed.abArmSwitchOn;
    doc["ab_flame_on"]           = ed.abFlameOn;
    doc["ab_flame_raw"]          = HardwareConfig::hasAbFlame ? g_sensorAbFlame.rawCounts() : 0;
    doc["ab_sol_open"]           = ed.abSolOpen;
    doc["ab_pump_demand"]        = (float)(int)(ed.abPumpDemand * 1000) / 1000.0f;
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
    if (HardwareConfig::hasTorque && HardwareConfig::hasN2Rpm &&
        ed.torqueHealthy && ed.n2Healthy && ed.n2Rpm > 0) {
        doc["turbo_power_w"]     = (int)ed.turboPower;
    } else {
        doc["turbo_power_w"]     = nullptr;
    }
    doc["torque_healthy"]        = ed.torqueHealthy;
    doc["fuel_press"]            = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_press_healthy"]    = ed.fuelPressHealthy;
    doc["max_fuel_press"]        = (float)(int)(ed.maxFuelPressure * 100) / 100.0f;
    doc["glow_plug_pct"]         = (int)(ed.glowPlugDemand * 100.0f);
    doc["wet_glow_fuel_pct"]     = (int)(ed.wetGlowFuelDemand * 100.0f);
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
        doc["has_starter"]           = HardwareConfig::hasStarter;
        doc["starter_type"]          = HardwareConfig::starterType;
        doc["has_starter_en"]        = HardwareConfig::hasStarterEn;
        doc["has_fuel_sol"]          = HardwareConfig::hasFuelSol;
        doc["has_igniter"]           = HardwareConfig::hasIgniter;
        doc["has_igniter2"]          = HardwareConfig::hasIgniter2;
        doc["has_oil_pump"]          = HardwareConfig::hasOilPump;
        doc["has_dynamic_idle"]      = HardwareConfig::hasDynamicIdle;
        doc["ws_interval_ms"]        = Config::wsIntervalMs;
        doc["has_oil_loop"]          = HardwareConfig::instance().hasOilLoop;
        bool relightIgnitionOk = false;
        switch (Config::relightIgnitionTarget) {
            case 1: relightIgnitionOk = HardwareConfig::hasIgniter2; break;
            case 2: relightIgnitionOk = HardwareConfig::hasGlowPlug; break;
            default: relightIgnitionOk = HardwareConfig::hasIgniter; break;
        }
        doc["relight_enabled"]       = Config::relightEnabled
                                       && HardwareConfig::hasN1Rpm
                                       && relightIgnitionOk;
        doc["flameout_source"]       = Config::flameoutSource;
        doc["flameout_n1_min_rpm"]   = Config::flameoutN1MinRpm;
        doc["flameout_tot_drop_c"]   = Config::flameoutTotDropC;
        doc["relight_ignition_target"] = Config::relightIgnitionTarget;
        doc["relight_confirm_source"] = Config::relightConfirmSource;
        doc["relight_min_rpm"]       = Config::relightMinRpm;
        doc["relight_confirm_rpm"]   = Config::relightConfirmRpm;
        doc["relight_tot_rise_c"]    = Config::relightTotRiseC;
        doc["dev_mode_fw"]           = true;
        doc["config_locked"]         = Config::isLocked();
        doc["config_storage_fault"]  = ed.configStorageFault;
        // Boot-load accept+warn notice (out-of-cap safety limits etc.)
        doc["config_load_warning"]   = Config::loadWarning[0] ? Config::loadWarning : nullptr;
        doc["ui_theme"]              = Config::uiTheme;
        doc["config_version_firmware"] = Config::CONFIG_VERSION;
        doc["hardware_profile"]      = HardwareConfig::profileId;
        doc["hw_json_loaded"]        = true;
        // Session / boot stats
        doc["run_count"]             = ed.runCount;
        doc["reset_reason"]          = ed.resetReason;
        doc["total_run_seconds"]     = Config::totalRunSeconds;
        // Flash usage (cached by tick() — never call LittleFS from async_tcp context)
        doc["log_max_records"]       = FlightRecorder::MAX_RECORDS;
        doc["flash_total_kb"]        = (int)s_fsTotal;
        doc["flash_used_kb"]         = (int)s_fsUsed;
        doc["flash_free_kb"]         = (int)(s_fsTotal - s_fsUsed);
        doc["max_p1"]                = (float)(int)(std::max(0.0f, maxP1Bar) * 100) / 100.0f;
        doc["max_p2"]                = (float)(int)(std::max(0.0f, maxP2Bar) * 100) / 100.0f;
        // Safety limits (for color gauge thresholds)
        doc["rpm_limit"]             = (int)Config::rpmLimit;
        doc["tot_limit"]             = Config::totLimit;
        doc["egt_source"]            = Config::effectiveEgtSource();
        doc["egt_limit"]             = Config::primaryEgtLimitC();
        doc["oil_running_min"]       = Config::oilRunningMin;
        doc["oil_temp_limit"]        = Config::oilTempLimit;
        doc["tit_limit"]             = Config::titLimit;
        doc["batt_volt_min"]         = Config::battVoltMin;
        doc["fuel_press_min"]        = Config::fuelPressMin;
        // has_* capability flags
        doc["has_afterburner"]       = HardwareConfig::hasAfterburner;
        doc["has_ab_flame"]          = HardwareConfig::hasAfterburner && HardwareConfig::hasAbFlame;
        doc["ab_flame_threshold"]    = HardwareConfig::abFlameThreshold;
        doc["has_n1"]                = HardwareConfig::hasN1Rpm;
        doc["has_n2"]                = HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm;
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
        doc["glow_plug_type"]        = HardwareConfig::glowPlugType;
        doc["glow_plug_output_type"] = HardwareConfig::glowPlugOutputType;
        doc["has_wet_glow"]          = HardwareConfig::hasGlowPlug && HardwareConfig::glowPlugType == 2;
        doc["wet_glow_fuel_type"]    = HardwareConfig::wetGlowFuelType;
        doc["has_glow_current"]      = HardwareConfig::hasGlowPlug && HardwareConfig::hasGlowCurrentSensor;
        doc["has_igniter_current"]   = HardwareConfig::hasIgniter && HardwareConfig::hasIgniterCurrentSensor;
        doc["has_igniter2_current"]  = HardwareConfig::hasIgniter2 && HardwareConfig::hasIgniter2CurrentSensor;
        doc["has_oil_pump_current"]  = HardwareConfig::hasOilPump && HardwareConfig::hasOilPumpCurrentSensor;
        doc["has_bleed_valve"]       = HardwareConfig::hasBleedValve;
        doc["has_prop_pitch"]        = HardwareConfig::hasPropPitch;
        doc["prop_pitch_type"]       = HardwareConfig::propPitchType;
        doc["has_fuel_pump2"]        = HardwareConfig::hasFuelPump2;
        doc["fuel_pump2_type"]       = HardwareConfig::fuelPump2Type;
        doc["has_cool_fan"]          = HardwareConfig::hasCoolFan;
        doc["has_airstarter"]        = HardwareConfig::hasAirstarterSol;
        doc["has_oil_scavenge"]      = HardwareConfig::hasOilScavengePump;
        doc["has_mavlink"]           = HardwareConfig::hasMAVLink;
        doc["has_tit"]               = HardwareConfig::hasTit;
        doc["has_starter_assist"]    = HardwareConfig::hasStarter
                                       && HardwareConfig::starterAssistEnabled
                                       && (HardwareConfig::starterType != 2)
                                       && HardwareConfig::hasN1Rpm;
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
        doc["seq_has_structural_errors"] = ed.seqHasStructuralErrors;
        auto issArr = doc["seq_issues"].to<JsonArray>();
        for (int i = 0; i < ed.seqIssueCount; i++) {
            auto obj = issArr.add<JsonObject>();
            obj["block"] = ed.seqIssues[i].blockName;
            obj["msg"]   = ed.seqIssues[i].reason;
            obj["error"] = ed.seqIssues[i].isError;
        }
        // ── DI channel config (label / role — state + pin already in fast) ──
        // Clear the fast array before adding full objects; ArduinoJson::to<JsonArray>()
        // returns the existing array when one is already present.
        doc["di_channels"].clear();
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

    // Revalidate shared assets on every page load. During beta testing the web
    // filesystem is reflashed often; stale JS/CSS with fresh HTML creates
    // confusing half-loaded pages.
    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendGzipAsset(req, "/app.js.gz", "application/javascript", SHARED_ASSET_CACHE);
    });
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendGzipAsset(req, "/style.css.gz", "text/css", SHARED_ASSET_CACHE);
    });
    _server.on("/theme.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendGzipAsset(req, "/theme.js.gz", "application/javascript", SHARED_ASSET_CACHE);
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

    // GET /api/data — live snapshot. Uses g_webTxBuf (static) to avoid a 6 KB stack
    // allocation inside the async TCP task callback (task stack is ~8 KB).
    _server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        static JsonDocument doc;   // static: avoids re-allocating ArduinoJson heap every call
        size_t n = _buildTelemetry(g_webTxBuf, sizeof(g_webTxBuf), doc, true);
        if (n >= sizeof(g_webTxBuf)) {
            AsyncWebServerResponse* resp = req->beginResponse(
                500, "application/json", "{\"error\":\"telemetry frame too large\"}");
            _finalizeJsonResponse(resp);
            req->send(resp);
            return;
        }
        AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", g_webTxBuf);
        _finalizeJsonResponse(resp);
        req->send(resp);
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
        AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", buf);
        _finalizeJsonResponse(resp);
        req->send(resp);
    });

    // GET /api/config — expose the settings section for page editors.
    // Serialize into the static TX buffer and send with a fixed
    // Content-Length (same path as /api/data). AsyncResponseStream silently
    // truncates a large JSON under AP heap pressure — serializeJson ignores
    // the stream's short writes — which the editor pages saw as
    // "Unterminated string in JSON".
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        Config::toJson(doc);
        if (measureJson(doc) + 1 > sizeof(g_webTxBuf)) {
            AsyncWebServerResponse* resp = req->beginResponse(
                500, "application/json", "{\"error\":\"config response too large\"}");
            _finalizeJsonResponse(resp);
            req->send(resp);
            return;
        }
        serializeJson(doc, g_webTxBuf, sizeof(g_webTxBuf));
        AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", g_webTxBuf);
        _finalizeJsonResponse(resp);
        req->send(resp);
    });

    // POST /api/config — replace only the settings section in ecu_config.json.
    // Body is accumulated across chunks before parsing — this section is ~2-3 KB
    // and may arrive in multiple TCP segments.
    _server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0 && _maintenanceUploadInProgress()) {
                req->send(423, "application/json", "{\"error\":\"maintenance upload in progress\"}");
                return;
            }
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
                if (!ok) {
                    req->send(400, "application/json", "{\"ok\":false,\"error\":\"settings rejected - check JSON and loaded engine profile_id\"}");
                    return;
                }
                bool applyQueued = CommandQueue::push({ OTCommand::APPLY_CONFIG });
                bool deferred = (EngineData::instance().mode != SysMode::STANDBY);
                if (!applyQueued) {
                    req->send(200, "application/json",
                        "{\"ok\":true,\"warn\":\"config saved, but live block reload queue was full; restart or re-save before testing\"}");
                } else if (deferred) {
                    req->send(200, "application/json",
                        "{\"ok\":true,\"warn\":\"config saved; block params will apply on next engine start\"}");
                } else {
                    req->send(200, "application/json", "{\"ok\":true}");
                }
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
            if (index == 0 && _maintenanceUploadInProgress()) {
                req->send(423, "application/json", "{\"error\":\"maintenance upload in progress\"}");
                return;
            }
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
            if (!ok) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"settings rejected\"}");
                return;
            }
            bool applyQueued = CommandQueue::push({ OTCommand::APPLY_CONFIG });
            FlightRecorder::logConfigChange("config.patch", 0, 0);
            // Config values are live in memory immediately.
            // Block-instance params (applyConfig) are applied on next START; warn if deferred.
            bool deferred = (EngineData::instance().mode != SysMode::STANDBY);
            if (!applyQueued) {
                req->send(200, "application/json",
                    "{\"ok\":true,\"warn\":\"config saved, but live block reload queue was full; restart or re-save before testing\"}");
                return;
            }
            if (deferred) {
                req->send(200, "application/json",
                    "{\"ok\":true,\"warn\":\"config saved; block params will apply on next engine start\"}");
            } else {
                req->send(200, "application/json", "{\"ok\":true}");
            }
        });

    // POST /api/theme?t=<key> — persist the web UI theme into ecu_config.json so it
    // travels with the engine file. Cosmetic: not mode-gated, no APPLY_CONFIG, no flight log.
    _server.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("t")) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing t\"}");
            return;
        }
        String t = req->getParam("t")->value();
        static const char* const VALID[] = { "carbon", "ember", "slate", "midnight", "contrast", "daylight" };
        bool ok = false;
        for (const char* v : VALID) if (t == v) { ok = true; break; }
        if (!ok) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown theme\"}");
            return;
        }
        strncpy(Config::uiTheme, t.c_str(), sizeof(Config::uiTheme) - 1);
        Config::uiTheme[sizeof(Config::uiTheme) - 1] = '\0';
        bool saved = Config::save();
        req->send(200, "application/json", saved ? "{\"ok\":true}" : "{\"ok\":true,\"warn\":\"not persisted\"}");
    });

    // GET /api/log — last 400 events as a JSON array for in-browser display.
    // Capped so AsyncResponseStream stays bounded regardless of log size.
    // For a full download use /api/log/raw (served directly from flash, zero heap buffer).
    _server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!_isStandbyLike(EngineData::instance().mode) || Config::logStandby) {
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
            char lineBuf[640];
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
        _finalizeJsonResponse(resp);
        req->send(resp);
    });

    // GET /api/log/raw — full flight log download as NDJSON (one JSON object per line).
    // Uses AsyncFileResponse: reads LittleFS in 1460-byte TCP chunks without heap buffering.
    _server.on("/api/log/raw", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!_isStandbyLike(EngineData::instance().mode) || Config::logStandby) {
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
        resp->addHeader("Cache-Control", "no-store");
        resp->addHeader("Connection", "close");
        req->send(resp);
    });

    // GET /api/log/csv — spreadsheet-friendly recent event export.
    // AsyncResponseStream is heap-buffered, so keep this bounded like /api/log.
    // Use /api/log/raw for the complete zero-copy NDJSON download.
    _server.on("/api/log/csv", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!_isStandbyLike(EngineData::instance().mode) || Config::logStandby) {
            req->send(423, "application/json",
                "{\"error\":\"Log download requires STANDBY with standby logging disabled\"}");
            return;
        }
        const int DISPLAY_LIMIT = 400;
        AsyncResponseStream* resp = req->beginResponseStream("text/csv");
        resp->addHeader("Content-Disposition", "attachment; filename=\"flight_log.csv\"");
        resp->print("t,ev,details\r\n");
        FlightRecorder::lockLog();
        File f = LittleFS.open(FlightRecorder::PATH, "r");
        int total = FlightRecorder::recordCount();
        int skip  = total > DISPLAY_LIMIT ? total - DISPLAY_LIMIT : 0;
        int seen  = 0;
        if (f) {
            JsonDocument doc;   // declared once outside the loop — avoids 2200× heap alloc/free
            char lineBuf[640];
            while (f.available()) {
                int n = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                if (n <= 0) continue;
                if (lineBuf[n - 1] == '\r') n--;
                lineBuf[n] = '\0';
                if (n == 0 || lineBuf[0] != '{') continue;
                if (seen++ < skip) continue;
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
        resp->addHeader("Cache-Control", "no-store");
        resp->addHeader("Connection", "close");
        req->send(resp);
    });

    // POST /api/start
    _server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (_maintenanceUploadInProgress()) {
            req->send(423, "application/json", "{\"ok\":false,\"error\":\"Maintenance upload in progress\"}");
            return;
        }
        // Report the reject reason via the HTTP response only.  EngineData
        // strings are Core-1-owned; writing them from async_tcp (Core 0) races
        // the ECU loop's own fault/event writes (CommandQueue-only rule).
        if (const char* reject = _startPreflightRejectReason()) {
            _sendCommandReject(req, 409, reject);
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
            if (const char* reject = _commandPreflightRejectReason(pkt)) {
                _sendCommandReject(req, 409, reject);
                return;
            }
            bool queued = pkt.cmd == OTCommand::AB_STOP
                        ? CommandQueue::pushEmergencyFront(pkt) : CommandQueue::push(pkt);
            if (queued) {
                req->send(200, "application/json", "{\"ok\":true}");
            } else {
                req->send(503, "application/json", "{\"ok\":false,\"error\":\"Command queue full\"}");
            }
        });

    // DELETE /api/session/all — wipe every session_N.csv file from /logs
    _server.on("/api/session/all", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (_maintenanceUploadInProgress()) {
            req->send(423, "application/json", "{\"error\":\"maintenance upload in progress\"}");
            return;
        }
        if (!_isStandbyLike(EngineData::instance().mode)) {
            req->send(423, "application/json",
                "{\"error\":\"Engine must be in STANDBY to delete session logs\"}");
            return;
        }
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

    // POST /api/factory_reset - reset to defaults, erase logs, reboot.
    // Removes ecu_config.json so the next boot regenerates from the compiled
    // hardware_profile.h defaults (identical to a fresh device). If an optional
    // /factory_config.json override is present it is restored instead; none
    // ships by default, so factory reset == first boot.
    _server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (_maintenanceUploadInProgress()) {
            req->send(423, "application/json", "{\"error\":\"maintenance upload in progress\"}");
            return;
        }
        if (!_isStandbyLike(EngineData::instance().mode)) {
            req->send(423, "application/json",
                "{\"error\":\"Engine must be in STANDBY to perform factory reset\"}");
            return;
        }
        // Same idle-outputs rule as OTA/restore: this path reboots, and a
        // standby tool (glow, oil prime, starter test, extra cooldown) must
        // not be left mid-action when ESP.restart() fires.
        if (_outputsActiveForOta()) {
            req->send(423, "application/json",
                "{\"error\":\"Stop active actuator tools/cooldown before factory reset\"}");
            return;
        }
        // Consume any Core-1 deferred save (e.g. hour meter) before wiping the
        // config, so a stale _savePending cannot make tick() rewrite the old
        // in-memory settings over it.  Writes the old config, which is removed next.
        Config::flushPendingSave();
        LittleFS.remove(Config::PATH);
        LittleFS.remove(HardwareConfig::PATH);
        // Optional override: if a curated /factory_config.json is present, restore
        // it; otherwise leave the config removed so the reboot regenerates from
        // the compiled hardware_profile.h defaults (the normal case).
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
        Serial.println("[WebServer] Factory reset - regenerating defaults, erased logs, rebooting");
        req->send(200, "application/json", "{\"ok\":true}");
        _scheduleRestart("factory reset");
    });

    // GET /api/session/list — JSON array of available run numbers, newest first
    _server.on("/api/session/list", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Collect all run numbers from /logs/session_N.csv files
        int runs[64];
        int count = 0;
        File dir = LittleFS.open("/logs");
        if (dir) {
            File entry = dir.openNextFile();
            while (entry) {
                int num = -1;
                // entry.name() may return full path (/logs/session_1.csv) or just basename
                const char* ename = entry.name();
                const char* fname = strrchr(ename, '/');
                fname = fname ? fname + 1 : ename;
                if (count < 64 && sscanf(fname, "session_%d.csv", &num) == 1)
                    runs[count++] = num;
                entry.close();
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
        _finalizeJsonResponse(resp);
        req->send(resp);
    });

    // GET /api/session/log?run=N — download a specific session CSV
    // Without ?run=N serves the most recent (current) session.
    _server.on("/api/session/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!_isStandbyLike(EngineData::instance().mode)) {
            req->send(423, "application/json",
                "{\"error\":\"Session logs are available after the engine returns to STANDBY\"}");
            return;
        }
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
        resp->addHeader("Cache-Control", "no-store");
        resp->addHeader("Connection", "close");
        req->send(resp);
    });

    // POST /update — OTA firmware upload (works over AP, no internet needed)
    // Browser sends multipart/form-data with the compiled .bin file.
    // ESP32 writes it to the inactive OTA partition and reboots.
    _server.on("/update", HTTP_POST,
        // Response callback — runs after all upload chunks received
        [](AsyncWebServerRequest* req) {
            UploadLock lock;
            if (_otaUploadOwner != req) {
                req->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"Another OTA upload is in progress\"}");
                return;
            }
            bool ok = !_otaError && !Update.hasError();
            req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) _otaPendingRestart = true;
            else {
                _otaInProgress = false;
                _otaUploadOwner = nullptr;
            }
        },
        // Upload handler — called per chunk
        [](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            UploadLock lock;
            if (!index) {
                if (_otaUploadOwner && _otaUploadOwner != req) return;
                _otaUploadOwner = req;
                _otaError = false;
                _otaUploadLastMs = millis();
                if (_assetUploadInProgress || _configRestoreOwner) {
                    Serial.println("[OTA] Rejected: another maintenance upload is in progress");
                    _otaError = true;
                    return;
                }
                // Guard: never flash firmware while the engine is running.
                // FAULT is accepted — OTA is a legitimate repair path.
                if (!_isStandbyLike(EngineData::instance().mode)) {
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
            if (_otaUploadOwner != req) return;
            _otaUploadLastMs = millis();
            if (!_otaError && !_isStandbyLike(EngineData::instance().mode)) {
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
                    Serial.printf("[OTA] Success: %u bytes - rebooting\n", index + len);
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
            UploadLock lock;
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
                _scheduleRestart("web asset update");
            }
        },
        [](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            UploadLock lock;
            if (!_assetUploadOwner) {
                _assetUploadOwner = req;
                _assetUploadError = false;
                _assetUploadMask = 0;
                _assetUploadInProgress = true;
                _assetUploadLastMs = millis();
                if (!_isStandbyLike(EngineData::instance().mode) ||
                    _otaInProgress || _configRestoreOwner || _outputsActiveForOta()) {
                    Serial.println("[WebAssets] Rejected: idle STANDBY required");
                    _assetUploadError = true;
                }
            }
            if (_assetUploadOwner != req || _assetUploadError) return;
            _assetUploadLastMs = millis();
            if (!_isStandbyLike(EngineData::instance().mode) || _outputsActiveForOta()) {
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

    // GET /api/hardware — return the hardware section of ecu_config.json.
    // Buffered send with fixed Content-Length (see GET /api/config): the
    // larger hardware JSON is exactly what streaming truncated on the
    // Sequence/Hardware pages.
    _server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        HardwareConfig::toJson(doc, true);
        if (measureJson(doc) + 1 > sizeof(g_webTxBuf)) {
            AsyncWebServerResponse* resp = req->beginResponse(
                500, "application/json", "{\"error\":\"hardware response too large\"}");
            _finalizeJsonResponse(resp);
            req->send(resp);
            return;
        }
        serializeJson(doc, g_webTxBuf, sizeof(g_webTxBuf));
        AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", g_webTxBuf);
        _finalizeJsonResponse(resp);
        req->send(resp);
    });

    // POST /api/hardware — validate + replace the hardware section, schedule reboot
    // Engine must be in STANDBY (or FAULT). Changes take effect after reboot.
    _server.on("/api/hardware", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0 && _maintenanceUploadInProgress()) {
                req->send(423, "application/json", "{\"error\":\"maintenance upload in progress\"}");
                return;
            }
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

            // Only allow hardware changes in STANDBY (or FAULT — the repair path)
            if (!_isStandbyLike(EngineData::instance().mode)) {
                req->send(423, "application/json",
                    "{\"error\":\"Engine must be in STANDBY to change hardware config\"}");
                return;
            }
            // Hardware save schedules a reboot — same idle-outputs rule as
            // OTA/restore so a running standby tool isn't cut mid-action.
            if (_outputsActiveForOta()) {
                req->send(423, "application/json",
                    "{\"error\":\"Stop active actuator tools/cooldown before saving hardware config\"}");
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
                if (!HardwareConfig::save()) {
                    Serial.println("[WebServer] ERROR: failed to restore previous hardware after settings sync failure");
                }
                Config::load();
                req->send(500, "application/json",
                    "{\"ok\":false,\"error\":\"Failed to synchronize settings after hardware dependency cleanup\"}");
                return;
            }
            Serial.printf("[WebServer] POST /api/hardware: saved (%u bytes) - reboot in 1s\n",
                          (unsigned)g_webRxLen);
            req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
            _scheduleRestart("hardware config save");
        });

    // PATCH /api/hardware — partial update of hardware section (calibration fields only, no reboot)
    _server.on("/api/hardware", HTTP_PATCH,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0 && _maintenanceUploadInProgress()) {
                req->send(423, "application/json", "{\"error\":\"maintenance upload in progress\"}");
                return;
            }
            // Hardware config changes take effect immediately on the live control loop
            // (HardwareConfig static fields are read every tick).  Reject unless
            // STANDBY (or FAULT — the control loop is equally idle there).
            // Gate on index == 0 so a multi-chunk body cannot req->send(409) once per
            // chunk (double send corrupts ESPAsyncWebServer response state).
            if (index == 0 && !_isStandbyLike(EngineData::instance().mode)) {
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
            // Re-check on completion: the engine may have left STANDBY between chunks.
            if (!_isStandbyLike(EngineData::instance().mode)) {
                req->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"engine not in STANDBY\"}");
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
                } else if (strcmp(topKey, "ab_flame") == 0 && top.value().is<JsonObject>()) {
                    for (JsonPair field : top.value().as<JsonObject>()) {
                        if (strcmp(field.key().c_str(), "threshold") != 0) {
                            calibrationOnly = false;
                            break;
                        }
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
            bool applyQueued = CommandQueue::push({ OTCommand::APPLY_CONFIG });
            FlightRecorder::logConfigChange("hardware.patch", 0, 0);
            if (!applyQueued) {
                req->send(200, "application/json",
                    "{\"ok\":true,\"warn\":\"hardware calibration saved, but live reload queue was full; restart or re-save before testing\"}");
            } else {
                req->send(200, "application/json", "{\"ok\":true}");
            }
        });

    // GET /api/ecu_config — download full unified config (hardware + settings)
    // Deliberately serves the file verbatim incl. plaintext wifi_password: the JSON
    // must restore 1:1 on another ESP32 (portability by design — do not redact here).
    _server.on("/api/ecu_config", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!LittleFS.exists(Config::PATH)) {
            req->send(404, "application/json", "{\"error\":\"ecu_config.json not found\"}");
            return;
        }
        AsyncWebServerResponse* resp = req->beginResponse(
            LittleFS, Config::PATH, "application/json");
        resp->addHeader("Content-Disposition", "attachment; filename=\"ecu_config.json\"");
        resp->addHeader("Cache-Control", "no-store");
        resp->addHeader("Connection", "close");
        req->send(resp);
    });

    // POST /api/ecu_config — upload full unified config, apply all sections, reboot
    _server.on("/api/ecu_config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            UploadLock lock;
            if (index == 0) {
                if (_configRestoreOwner) {
                    req->send(409, "application/json",
                        "{\"error\":\"Another full configuration restore is in progress\"}");
                    return;
                }
                if (_otaInProgress || _assetUploadInProgress) {
                    req->send(409, "application/json",
                        "{\"error\":\"Another maintenance upload is in progress\"}");
                    return;
                }
                if (!_isStandbyLike(EngineData::instance().mode) || _outputsActiveForOta()) {
                    req->send(423, "application/json",
                        "{\"error\":\"Engine must be idle in STANDBY to upload config\"}");
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

            if (!_isStandbyLike(EngineData::instance().mode)) {
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
            bool previousConfigMismatch = EngineData::instance().configVersionMismatch;
            size_t stagedHwLen = serializeJson(hwDoc, g_webRxBuf, sizeof(g_webRxBuf));
            if (previousHwLen >= sizeof(g_webTxBuf) ||
                stagedHwLen >= sizeof(g_webRxBuf) ||
                !HardwareConfig::fromJson(g_webRxBuf, stagedHwLen) ||
                !Config::applyJsonRuntimeOnly(settingsDoc)) {
                if (previousHwLen < sizeof(g_webTxBuf)) HardwareConfig::fromJson(g_webTxBuf, previousHwLen);
                Config::applyJsonRuntimeOnly(previousSettings);
                EngineData::instance().configVersionMismatch = previousConfigMismatch;
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
                Config::applyJsonRuntimeOnly(previousSettings);
                EngineData::instance().configVersionMismatch = previousConfigMismatch;
                req->send(500, "application/json", "{\"error\":\"sanitized hardware section too large\"}");
                _finishConfigRestore();
                return;
            }
            Config::toJson(sanitizedSettings);
            fullDoc[HardwareConfig::SECTION].set(sanitizedHw);
            fullDoc[Config::SECTION].set(sanitizedSettings);
            HardwareConfig::fromJson(g_webTxBuf, previousHwLen);
            Config::applyJsonRuntimeOnly(previousSettings);
            EngineData::instance().configVersionMismatch = previousConfigMismatch;

            // Store one complete engine file only after both sections validate.
            // Runtime values are loaded from this committed file after reboot.
            if (!_writeUnifiedConfigAtomically(fullDoc)) {
                req->send(500, "application/json", "{\"error\":\"failed to atomically save ecu_config.json\"}");
                _finishConfigRestore();
                return;
            }

            Serial.printf("[WebServer] POST /api/ecu_config: %u bytes - reboot in 1s\n", (unsigned)total);
            _finishConfigRestore(false);
            req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
            _scheduleRestart("engine config restore");
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
            full = false;
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
        static char buf[6144];
        static JsonDocument doc;
        size_t n = _buildTelemetry(buf, sizeof(buf), doc, full);
        if (n < sizeof(buf)) {
            if (!_sendTelemetryFrame(client, buf, n)) _wsPendingResponse = true;
        } else if (full) {
            Serial.printf("[WebSocket] Full telemetry frame too large (%u >= %u), falling back to fast frame\n",
                          (unsigned)n, (unsigned)sizeof(buf));
            doc.clear();
            n = _buildTelemetry(buf, sizeof(buf), doc, false);
            if (n >= sizeof(buf) || !_sendTelemetryFrame(client, buf, n))
                _wsPendingResponse = true;
        } else {
            Serial.printf("[WebSocket] Fast telemetry frame too large (%u >= %u)\n",
                          (unsigned)n, (unsigned)sizeof(buf));
            _wsPendingResponse = true;
        }
    });
    _server.addHandler(&_ws);
}

// ── Public API ────────────────────────────────────────────────
void WebServer::begin() {
    _uploadMux = xSemaphoreCreateMutexStatic(&_uploadMuxBuf);
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
    // Skip while a reboot is pending: factory reset / config restore just replaced
    // the on-disk file, and a deferred save would overwrite it with the old
    // in-memory settings during the 5 s pre-reboot window.
    if (!_hwRebootPending) Config::flushPendingSave();
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
        if (now - _wsPingMs >= 200) {
            _wsPingMs = now;
            _ws.pingAll();
        }
    }

    // OTA: reboot after response has been sent
    if (_otaPendingRestart) {
        _restartCleanly("firmware OTA");
    }
    // Interrupted OTA upload: if the client disconnects mid-upload no further
    // chunk or completion callback ever runs, so without this timeout the
    // maintenance lock (423 on start/command/save) persists until power cycle
    // and _otaUploadOwner dangles.  Idle-timeout pattern matches the asset and
    // config-restore cleanups below; re-check under the lock closes the race
    // against a chunk arriving exactly at the boundary.
    if (_otaUploadOwner && !_otaPendingRestart && (millis() - _otaUploadLastMs) > 30000) {
        UploadLock lock;
        if (_otaUploadOwner && !_otaPendingRestart && (millis() - _otaUploadLastMs) > 30000) {
            Serial.println("[OTA] Timed out - aborting interrupted firmware upload");
            if (Update.isRunning()) Update.abort();
            _otaError = true;
            _otaInProgress = false;
            _otaUploadOwner = nullptr;
        }
    }
    if (_assetUploadInProgress && (millis() - _assetUploadLastMs) > 30000) {
        UploadLock lock;
        if (_assetUploadInProgress && (millis() - _assetUploadLastMs) > 30000) {
            Serial.println("[WebAssets] Timed out - discarding staged upload");
            _assetUploadError = true;
            _finishAssetUpload();
        }
    }
    if (_configRestoreOwner && (millis() - _configRestoreLastMs) > 30000) {
        UploadLock lock;
        if (_configRestoreOwner && (millis() - _configRestoreLastMs) > 30000) {
            Serial.println("[Config] Timed out - discarding staged full restore");
            _finishConfigRestore();
        }
    }
    // Reboot only after the HTTP response has had time to leave and network
    // clients have seen the AP disappear cleanly.
    if (_hwRebootPending && (long)(millis() - _hwRebootScheduledMs) >= 0) {
        _restartCleanly(_pendingRestartReason);
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

bool WebServer::rebootPending() {
    return _hwRebootPending;
}
