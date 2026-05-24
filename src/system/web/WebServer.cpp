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

static volatile bool _otaPendingRestart      = false;
static bool          _otaError               = false;
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

// Shared buffers — only one HTTP request is processed at a time so these are safe to share.
// Consolidating here (vs function-local statics) keeps them out of .bss individually and
// makes the total DRAM footprint explicit: 2 × 8192 = 16 KB instead of the ~68 KB that
// nine separate function-local statics would consume.
static char   g_webRxBuf[8192];   // request body accumulation (POST / PATCH)
static size_t g_webRxLen     = 0;
static bool   g_webRxOverflow = false;
static char   g_webTxBuf[8192];   // response serialisation + PATCH merge work buffer

static AsyncWebServer  _server(80);
static AsyncWebSocket  _ws("/ws");
static DNSServer       _dns;                 // captive portal DNS


// ── WiFi AP setup ─────────────────────────────────────────────
static void _startWiFi() {
    WiFi.mode(WIFI_AP);
    const char* ssid = HardwareConfig::profileId[0] ? HardwareConfig::profileId : "OpenTurbine";
    const char* pwd  = HardwareConfig::wifiPassword[0] ? HardwareConfig::wifiPassword : nullptr;
    WiFi.softAP(ssid, pwd);  // SSID = hardware profile_id; password optional
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
    Serial.printf("[WiFi] AP: %s  IP: %s  %s\n", ssid, apIP.toString().c_str(),
                  pwd ? "(password protected)" : "(open network)");

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
    doc["fuel_pressure"]         = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_flow"]             = (float)(int)(ed.fuelFlow * 100) / 100.0f;
    // ── Throttle / idle demand ─────────────────────────────────────────────
    doc["throttle_input_raw"]    = ed.throttleInputRaw;
    doc["throttle_demand"]       = (float)(int)(ed.throttleDemand * 1000) / 1000.0f;
    // Effective throttle = pilot demand + AB main fuel offset (what the ESC sees)
    doc["throttle_effective"]    = (float)(int)(
        constrain(ed.throttleDemand + ed.abFuelOffset, 0.0f, 1.0f) * 1000) / 1000.0f;
    doc["ab_fuel_offset"]        = (float)(int)(ed.abFuelOffset * 1000) / 1000.0f;
    doc["idle_input_raw"]        = ed.idleInputRaw;
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
    doc["relight_armed"]         = ed.relightArmed;
    doc["relight_attempts"]      = (int)ed.relightAttempts;
    doc["extra_cooldown_active"] = ed.extraCooldownActive;
    {
        unsigned long now = millis();
        int remS = (ed.extraCooldownActive && ed.extraCooldownUntilMs > now)
                   ? (int)((ed.extraCooldownUntilMs - now) / 1000UL) : 0;
        doc["extra_cooldown_remaining_s"] = remS;
    }
    doc["profile_match"]         = Config::profileMatch;
    doc["config_version_mismatch"] = ed.configVersionMismatch;
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
    doc["fuel_press_raw"]        = ed.fuelPressRaw;
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
        if (!HardwareConfig::hasThrottleInput) {
            doc["throttle_input_type"] = "none";
        } else if (HardwareConfig::throttleInputRcPwm) {
            doc["throttle_input_type"] = "servo";
            doc["throttle_input_us"]   = Config::rcMinUs + (int)(ed.rcThrottleNorm *
                                           (float)(Config::rcMaxUs - Config::rcMinUs));
        } else {
            doc["throttle_input_type"] = "adc";
        }
        if (!HardwareConfig::hasIdleInput) {
            doc["idle_input_type"]   = "none";
        } else if (HardwareConfig::idleInputRcPwm) {
            doc["idle_input_type"]   = "servo";
            doc["idle_input_us"]     = Config::rcMinUs + (int)(ed.rcIdleNorm *
                                         (float)(Config::rcMaxUs - Config::rcMinUs));
        } else {
            doc["idle_input_type"]   = "adc";
        }
        doc["rc_pwm_active"]         = HardwareConfig::throttleInputRcPwm
                                       || HardwareConfig::idleInputRcPwm;
        doc["idle_use_n2"]           = Config::idleUseN2;
        doc["limp_throttle_cap"]     = Config::limpMaxThrottlePct;
        doc["idle_min_pct"]          = Config::throttleIdleMinPct;
        doc["has_oil_loop"]          = HardwareConfig::instance().hasOilLoop;
        doc["relight_enabled"]       = Config::relightEnabled;
        doc["relight_min_rpm"]       = Config::relightMinRpm;
#ifdef OT_DEV_MODE
        doc["dev_mode_fw"]           = true;
#else
        doc["dev_mode_fw"]           = false;
#endif
        doc["config_locked"]         = Config::isLocked();
        doc["config_version_firmware"] = Config::CONFIG_VERSION;
        doc["fw_version"]            = OT_VERSION;
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
        doc["max_p1"]                = (float)(int)(ed.maxP1 * 100) / 100.0f;
        doc["max_p2"]                = (float)(int)(ed.maxP2 * 100) / 100.0f;
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
        doc["has_n2"]                = HardwareConfig::hasN2Rpm;
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
        doc["p1_raw"]                = g_sensorP1.rawCounts();
        doc["p2_raw"]                = g_sensorP2.rawCounts();
        doc["glow_current_raw"]      = g_sensorGlowCurrent.rawCounts();
        doc["igniter_current_raw"]   = g_sensorIgniterCurrent.rawCounts();
        doc["igniter2_current_raw"]  = g_sensorIgniter2Current.rawCounts();
        doc["oil_pump_current_raw"]  = g_sensorOilPumpCurrent.rawCounts();
        doc["igniter_coil"]          = HardwareConfig::igniterCoil;
        doc["fuel_flow_type"]        = HardwareConfig::fuelFlowType;
        doc["fuel_flow_raw"]         = (HardwareConfig::fuelFlowType == 0)
                                       ? g_sensorFuelFlow.rawCounts() : 0;
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

    // ── Captive portal resolution ─────────────────────────────
    // Return the responses each OS expects so it immediately marks the portal
    // as resolved and stops intercepting traffic.  Users then open 192.168.4.1
    // or http://ot.local in their regular browser (Safari/Chrome), which has
    // full WebSocket support — unlike the sandboxed captive-portal WebView.
    // Android: expects 204 No Content from /generate_204
    _server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(204);
    });
    // Apple iOS: expects a page whose body contains "Success"
    _server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    _server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    // Windows NCSI
    _server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Microsoft NCSI");
    });
    _server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Microsoft NCSI");
    });
    _server.on("/fwlink",       HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/redirect",     HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });

    // app.js and style.css are shared across all pages and only change when the
    // filesystem is reflashed — cache them for 1 hour to avoid re-fetching on every
    // page navigation, which would exhaust the lwIP TCP PCB pool alongside the
    // persistent SSE connection and cause "IP not responding" errors.
    _server.serveStatic("/app.js",    LittleFS, "/").setCacheControl("max-age=3600");
    _server.serveStatic("/style.css", LittleFS, "/").setCacheControl("max-age=3600");
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

    // GET /api/config — download current config
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t n = Config::toJson(g_webTxBuf, sizeof(g_webTxBuf));
        if (n >= sizeof(g_webTxBuf)) Serial.printf("[WebServer] WARNING: config JSON truncated (%u >= %u)\n", (unsigned)n, (unsigned)sizeof(g_webTxBuf));
        req->send(200, "application/json", g_webTxBuf);
    });

    // POST /api/config — upload new config (full replace)
    // Body is accumulated across chunks before parsing — config JSON is ~2-3 KB
    // and may arrive in multiple TCP segments.
    _server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) { g_webRxLen = 0; g_webRxOverflow = false; }
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;   // wait for more chunks
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }
            if (!Config::isLocked()) {
                bool ok = Config::fromJson(g_webRxBuf, g_webRxLen);
                // If fromJson succeeds it already saves; do NOT save on failure — it would
                // silently persist the old (unchanged) config and mislead the caller.
                Serial.printf("[WebServer] POST /api/config: len=%u ok=%d\n", (unsigned)g_webRxLen, ok ? 1 : 0);
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"fromJson rejected — check profile_id or JSON validity\"}");
            } else {
                req->send(423, "application/json", "{\"error\":\"locked\"}");
            }
        });

    // PATCH /api/config — partial update (calibration wizard, selective field saves)
    // Merges incoming JSON over the existing config document, then saves.
    _server.on("/api/config", HTTP_PATCH,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) { g_webRxLen = 0; g_webRxOverflow = false; }
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;   // wait for more chunks
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
            // Use the same 2-level deep merge as PATCH /api/hardware so that sending
            // e.g. {"calibration":{"p1_gain":1.05}} does not wipe the other calibration
            // fields — it only updates the keys that are present in the patch.
            JsonDocument current;
            Config::toJson(current);
            for (JsonPair kv : patch.as<JsonObject>()) {
                JsonVariant dst = current[kv.key()];
                if (dst.is<JsonObject>() && kv.value().is<JsonObject>()) {
                    for (JsonPair inner : kv.value().as<JsonObject>())
                        dst[inner.key()] = inner.value();
                } else {
                    current[kv.key()] = kv.value();
                }
            }
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
        if (!LittleFS.exists(FlightRecorder::PATH)) {
            req->send(404, "text/plain", "No log");
            return;
        }
        AsyncWebServerResponse* resp = req->beginResponse(
            LittleFS, FlightRecorder::PATH, "application/x-ndjson");
        resp->addHeader("Content-Disposition", "attachment; filename=\"flight_log.ndjson\"");
        req->send(resp);
    });

    // GET /api/log/csv — flat CSV download, streamed from flash
    _server.on("/api/log/csv", HTTP_GET, [](AsyncWebServerRequest* req) {
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
        CommandQueue::push({ OTCommand::START });
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/stop
    _server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        CommandQueue::push({ OTCommand::STOP });
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/command — generic command dispatch
    _server.on("/api/command", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) { g_webRxLen = 0; g_webRxOverflow = false; }
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;
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
            CommandQueue::push(pkt);
            req->send(200, "application/json", "{\"ok\":true}");
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
                    LittleFS.remove(path);
                }
                entry = dir.openNextFile();
            }
            dir.close();
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/factory_reset — erase all config + hardware files, reboot to defaults
    _server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (EngineData::instance().mode != SysMode::STANDBY) {
            req->send(423, "application/json",
                "{\"error\":\"Engine must be in STANDBY to perform factory reset\"}");
            return;
        }
        LittleFS.remove(Config::PATH);
        LittleFS.remove(HardwareConfig::PATH);
        LittleFS.remove(FlightRecorder::PATH);
        Serial.println("[WebServer] Factory reset — config erased, rebooting");
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
            bool ok = !Update.hasError();
            req->send(200, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Update failed\"}");
            if (ok) _otaPendingRestart = true;
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
                Serial.printf("[OTA] Upload start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                    _otaError = true;
                }
            }
            if (!_otaError) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                    _otaError = true;
                }
            }
            if (final) {
                if (!_otaError && Update.end(true)) {
                    Serial.printf("[OTA] Success: %u bytes — rebooting\n", index + len);
                } else if (!_otaError) {
                    Update.printError(Serial);
                    _otaError = true;
                }
            }
        });

    // GET /api/hardware — return hardware.json as-is
    _server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t n = HardwareConfig::toJson(g_webTxBuf, sizeof(g_webTxBuf));
        if (n >= sizeof(g_webTxBuf))
            Serial.printf("[WebServer] WARNING: hardware JSON truncated (%u >= %u)\n",
                          (unsigned)n, (unsigned)sizeof(g_webTxBuf));
        req->send(200, "application/json", g_webTxBuf);
    });

    // POST /api/hardware — validate + save hardware.json, schedule reboot
    // Engine must be in STANDBY. Changes take effect after reboot.
    _server.on("/api/hardware", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) { g_webRxLen = 0; g_webRxOverflow = false; }
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;   // wait for more chunks
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
            bool ok = HardwareConfig::fromJson(g_webRxBuf, g_webRxLen);
            if (!ok) {
                req->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Invalid hardware JSON\"}");
                return;
            }
            if (!HardwareConfig::save()) {
                req->send(500, "application/json",
                    "{\"ok\":false,\"error\":\"Failed to write hardware.json\"}");
                return;
            }
            Serial.printf("[WebServer] POST /api/hardware: saved (%u bytes) — reboot in 1s\n",
                          (unsigned)g_webRxLen);
            _hwRebootPending     = true;
            _hwRebootScheduledMs = millis();
            req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
        });

    // PATCH /api/hardware — partial update (calibration fields only, no reboot)
    _server.on("/api/hardware", HTTP_PATCH,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            // Hardware config changes take effect immediately on the live control loop
            // (HardwareConfig static fields are read every tick).  Reject unless STANDBY.
            if (EngineData::instance().mode != SysMode::STANDBY) {
                req->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"engine not in STANDBY\"}");
                return;
            }
            if (index == 0) { g_webRxLen = 0; g_webRxOverflow = false; }
            if (g_webRxLen + len < sizeof(g_webRxBuf)) {
                memcpy(g_webRxBuf + g_webRxLen, data, len);
                g_webRxLen += len;
            } else {
                g_webRxOverflow = true;
            }
            if (index + len < total) return;
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }
            JsonDocument patch;
            if (deserializeJson(patch, g_webRxBuf, g_webRxLen) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
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
            for (JsonPair kv : patch.as<JsonObject>()) {
                JsonVariant dst = current[kv.key()];
                if (dst.is<JsonObject>() && kv.value().is<JsonObject>()) {
                    for (JsonPair inner : kv.value().as<JsonObject>())
                        dst[inner.key()] = inner.value();
                } else {
                    current[kv.key()] = kv.value();
                }
            }
            size_t merged = serializeJson(current, g_webTxBuf, sizeof(g_webTxBuf));
            if (merged >= sizeof(g_webTxBuf)) {
                // Buffer was too small — output is truncated; reject rather than corrupt config
                req->send(500, "application/json", "{\"error\":\"merged hardware config too large\"}");
                return;
            }
            HardwareConfig::fromJson(g_webTxBuf, merged);
            HardwareConfig::save();
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
            if (index == 0) { g_webRxLen = 0; g_webRxOverflow = false; }
            if (g_webRxLen + len < sizeof(g_webRxBuf)) { memcpy(g_webRxBuf + g_webRxLen, data, len); g_webRxLen += len; }
            else { g_webRxOverflow = true; }
            if (index + len < total) return;
            if (g_webRxOverflow) {
                req->send(400, "application/json", "{\"error\":\"request body too large\"}");
                return;
            }

            if (EngineData::instance().mode != SysMode::STANDBY) {
                req->send(423, "application/json",
                    "{\"error\":\"Engine must be in STANDBY to upload config\"}");
                return;
            }
            JsonDocument fullDoc;
            if (deserializeJson(fullDoc, g_webRxBuf, g_webRxLen) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            // Apply hardware section — re-serialise into g_webTxBuf (heap) to avoid
            // large stack arrays.  g_webRxBuf holds the incoming data and is no longer
            // needed at this point, so g_webTxBuf is free to reuse.
            if (fullDoc[HardwareConfig::SECTION].is<JsonObject>()) {
                JsonDocument hwDoc;
                hwDoc.set(fullDoc[HardwareConfig::SECTION]);
                size_t hwLen = serializeJson(hwDoc, g_webTxBuf, sizeof(g_webTxBuf));
                if (hwLen >= sizeof(g_webTxBuf)) {
                    req->send(500, "application/json", "{\"error\":\"hardware section too large\"}");
                    return;
                }
                if (!HardwareConfig::fromJson(g_webTxBuf, hwLen)) {
                    req->send(400, "application/json", "{\"error\":\"hardware section rejected\"}");
                    return;
                }
            }
            // Apply settings section — same buffer, used sequentially (not simultaneously)
            if (fullDoc[Config::SECTION].is<JsonObject>()) {
                JsonDocument sd;
                sd.set(fullDoc[Config::SECTION]);
                size_t cfgLen = serializeJson(sd, g_webTxBuf, sizeof(g_webTxBuf));
                if (cfgLen >= sizeof(g_webTxBuf)) {
                    req->send(500, "application/json", "{\"error\":\"settings section too large\"}");
                    return;
                }
                if (!Config::fromJson(g_webTxBuf, cfgLen)) {
                    req->send(400, "application/json", "{\"error\":\"settings section rejected\"}");
                    return;
                }
            }
            // Write the full file verbatim so both sections are present
            File fw = LittleFS.open(Config::PATH, "w");
            if (fw) { serializeJsonPretty(fullDoc, fw); fw.close(); }

            Serial.printf("[WebServer] POST /api/ecu_config: %u bytes — reboot in 1s\n", (unsigned)g_webRxLen);
            _hwRebootPending     = true;
            _hwRebootScheduledMs = millis();
            req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
        });

    // 404
    _server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404);
    });

    // WebSocket — client-pull model with PING/PONG rescue.
    //
    // Core problem: async_tcp (AsyncTCP 3.x + IDF5) intermittently blocks for
    // 2–20 s waiting for events, even when "p" messages are arriving every 500 ms.
    // This is the same root cause that required CONFIG_ASYNC_TCP_USE_WDT=0.
    //
    // Primary path: JS sends "p" every 500 ms → WS_EVT_DATA fires inside async_tcp
    // task → server replies immediately (no cross-task handoff needed).  A fast
    // frame (~2 KB) is sent on every pull; a full frame (~7 KB) is sent on connect
    // and every ~30 s, so JS always has current labels, limits, and config.
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
            full       = true;   // always full on connect so dashboard populates immediately
        } else if (type == WS_EVT_DATA) {
            // Send full frame on connect and roughly every 30 s (60 × 500 ms)
            static uint8_t _fullCounter = 0;
            if (++_fullCounter >= 60) { _fullCounter = 0; full = true; }
            // Record the full flag BEFORE the canSend() check so the PONG rescue
            // can honour it if canSend() is false and the frame must be deferred.
            _wsPendingFull     = full;
            _wsPendingResponse = true;
            shouldSend = true;
        } else if (type == WS_EVT_PONG && _wsPendingResponse) {
            // Rescue: tick() sent a PING because canSend() was false; now we are
            // back inside async_tcp context and the pipe should be clear.
            // Restore the full flag that was saved when the original pull arrived.
            shouldSend = true;
            full       = _wsPendingFull;
        }

        if (!shouldSend || !client || !client->canSend()) return;
        _wsPendingResponse = false;
        static char buf[7168];
        static JsonDocument doc;
        size_t n = _buildTelemetry(buf, sizeof(buf), doc, full);
        if (n < sizeof(buf)) client->text(buf);
    });
    _server.addHandler(&_ws);
}

// ── Public API ────────────────────────────────────────────────
void WebServer::begin() {
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
    if (_t4 - _t0 > 200) {
        Serial.printf("[tick] SLOW %lums: dns=%lu evict=%lu drain=%lu save=%lu\n",
            _t4-_t0, _t1-_t0, _t2-_t1, _t3-_t2, _t4-_t3);
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
        delay(200);
        ESP.restart();
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
