#include "WebServer.h"
#include "hardware_profile.h"
#include "../version.h"
#include "../Config.h"
#include "../HardwareConfig.h"
#include "../FlightRecorder.h"
#include "../SessionLogger.h"
#include "../../engine/EngineData.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Arduino.h>
#include <Update.h>

static volatile bool _otaPendingRestart      = false;
static volatile bool _hwRebootPending        = false;
static unsigned long _hwRebootScheduledMs    = 0;

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

unsigned long WebServer::_lastWsMs = 0;

// ── WiFi AP setup ─────────────────────────────────────────────
static void _startWiFi() {
    WiFi.mode(WIFI_AP);
    const char* ssid = HardwareConfig::profileId[0] ? HardwareConfig::profileId : "OpenTurbine";
    const char* pwd  = HardwareConfig::wifiPassword[0] ? HardwareConfig::wifiPassword : nullptr;
    WiFi.softAP(ssid, pwd);  // SSID = hardware profile_id; password optional
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
static size_t _buildTelemetry(char* buf, size_t len) {
    auto& ed = EngineData::instance();
    JsonDocument doc;
    doc["mode"]                 = sysModeStr(ed.mode);
    doc["n1"]                   = (int)ed.n1Rpm;
    doc["n2"]                   = (int)ed.n2Rpm;
    doc["tot"]                  = (float)(int)(ed.tot * 10) / 10.0f;
    doc["tit"]                  = (float)(int)(ed.tit * 10) / 10.0f;
    doc["oil"]                  = (float)(int)(ed.oilPressure * 100) / 100.0f;
    doc["oil_raw"]              = ed.oilPressureRaw;
    doc["oil_demand"]           = (float)(int)(ed.oilDemand * 100) / 100.0f;
    doc["flame"]                = ed.flameDetected;
    doc["flame_raw"]            = ed.flameSensorRaw;
    doc["p1"]                   = (float)(int)(std::max(0.0f, ed.p1 - Config::p1ZeroBar) * 100) / 100.0f;
    doc["p2"]                   = (float)(int)(std::max(0.0f, ed.p2 - Config::p2ZeroBar) * 100) / 100.0f;
    doc["fuel_pressure"]        = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_flow"]            = (float)(int)(ed.fuelFlow * 100) / 100.0f;
    doc["has_fuel_flow"]        = HardwareConfig::hasFuelFlow;
    doc["pressure_sensors_enabled"] = Config::pressureSensorsEnabled;
    doc["flame_threshold"]      = Config::flameThreshold;
    // ── Throttle input ────────────────────────────────────────
    doc["throttle_input_raw"]   = ed.throttleInputRaw;
    doc["throttle_demand"]      = (float)(int)(ed.throttleDemand * 1000) / 1000.0f;
    if (!HardwareConfig::hasThrottleInput) {
        doc["throttle_input_type"] = "none";
    } else if (HardwareConfig::throttleInputRcPwm) {
        doc["throttle_input_type"] = "servo";
        doc["throttle_input_us"]   = Config::rcMinUs + (int)(ed.rcThrottleNorm *
                                       (float)(Config::rcMaxUs - Config::rcMinUs));
    } else {
        doc["throttle_input_type"] = "adc";
    }
    // ── Idle input ────────────────────────────────────────────
    doc["idle_input_raw"]       = ed.idleInputRaw;
    if (!HardwareConfig::hasIdleInput) {
        doc["idle_input_type"]  = "none";
    } else if (HardwareConfig::idleInputRcPwm) {
        doc["idle_input_type"]  = "servo";
        doc["idle_input_us"]    = Config::rcMinUs + (int)(ed.rcIdleNorm *
                                    (float)(Config::rcMaxUs - Config::rcMinUs));
    } else {
        doc["idle_input_type"]  = "adc";
    }
    doc["oil_pct"]              = (int)ed.oilPctDemand;
    doc["n1_healthy"]           = ed.n1Healthy;
    doc["n2_healthy"]           = ed.n2Healthy;
    doc["tot_healthy"]          = ed.totHealthy;
    doc["tit_healthy"]          = ed.titHealthy;
    doc["oil_healthy"]          = ed.oilHealthy;
    doc["dynamic_idle_enabled"] = ed.dynamicIdleEnabled;
    doc["limp_mode"]            = ed.limpMode;
    doc["limp_throttle_cap"]    = Config::limpMaxThrottlePct;
    doc["stop_switch_active"]    = ed.stopSwitchActive;
    doc["start_switch_active"]   = ed.startSwitchActive;
    doc["starter_assist_active"] = ed.starterAssistActive;
    doc["manual_relight_active"] = ed.manualRelightActive;
    doc["oil_failsafe_active"]   = ed.oilFailsafeActive;
    doc["oil_min_bar"]           = (float)(int)(ed.oilMinBar * 100) / 100.0f;
    doc["relight_enabled"]       = Config::relightEnabled;
    doc["relight_min_rpm"]       = Config::relightMinRpm;
    doc["last_event"]            = ed.lastEvent;
    doc["dev_mode"]             = ed.devMode;
#ifdef OT_DEV_MODE
    doc["dev_mode_fw"]          = true;   // OT_DEV_MODE compiled in — safety checks can be bypassed
#else
    doc["dev_mode_fw"]          = false;
#endif
    doc["skip_safety_checks"]   = ed.skipSafetyChecks;
    doc["bench_mode"]           = ed.benchMode;
    doc["relight_armed"]        = ed.relightArmed;
    doc["relight_attempts"]     = (int)ed.relightAttempts;
    doc["extra_cooldown_active"]= ed.extraCooldownActive;
    doc["idle_min_pct"]         = Config::throttleIdleMinPct;
    doc["idle_target_rpm"]      = Config::idleTargetRpm;
    doc["config_locked"]        = Config::isLocked();
    doc["profile_match"]        = Config::profileMatch;
    doc["config_version_mismatch"] = ed.configVersionMismatch;
    doc["fw_version"]           = OT_VERSION;
    doc["uptime_s"]             = ed.uptimeMs / 1000;
    doc["max_n1"]               = (int)ed.maxN1;
    doc["max_n2"]               = (int)ed.maxN2;
    doc["max_tot"]              = (float)(int)(ed.maxTot * 10) / 10.0f;
    doc["max_p1"]               = (float)(int)(ed.maxP1 * 100) / 100.0f;
    doc["max_p2"]               = (float)(int)(ed.maxP2 * 100) / 100.0f;
    doc["idle_use_n2"]          = Config::idleUseN2;
    doc["rc_throttle_valid"]    = ed.rcThrottleValid;
    doc["rc_throttle_norm"]     = (float)(int)(ed.rcThrottleNorm * 1000) / 1000.0f;
    doc["rc_idle_valid"]        = ed.rcIdleValid;
    doc["rc_idle_norm"]         = (float)(int)(ed.rcIdleNorm * 1000) / 1000.0f;
    doc["rc_pwm_active"]        = HardwareConfig::throttleInputRcPwm
                                  || HardwareConfig::idleInputRcPwm;
    // ── Afterburner state ─────────────────────────────────────
    doc["has_afterburner"]      = HardwareConfig::hasAfterburner;
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
        doc["ab_mode"]          = abStr;
    }
    doc["ab_trigger_active"]    = ed.abTriggerActive;
    doc["ab_arm_switch_on"]     = ed.abArmSwitchOn;
    doc["ab_flame_on"]          = ed.abFlameOn;
    doc["ab_sol_open"]          = ed.abSolOpen;
    // ── Hardware profile info ─────────────────────────────────
    doc["hardware_profile"]     = HardwareConfig::profileId;
    doc["hw_json_loaded"]       = true;
    // ── Sequence progress ─────────────────────────────────────
    doc["current_block"]        = ed.currentBlock;
    doc["seq_block_idx"]        = (int)ed.seqBlockIdx;
    doc["seq_block_total"]      = (int)ed.seqBlockTotal;
    doc["seq_wait_reason"]      = ed.seqWaitReason[0] ? ed.seqWaitReason : nullptr;
    // ── Fault description ─────────────────────────────────────
    doc["fault_description"]    = ed.faultDescription;
    // ── Hour meter / stats ────────────────────────────────────
    doc["run_count"]            = ed.runCount;
    doc["total_run_seconds"]    = Config::totalRunSeconds;
    // ── Safety limits (for color gauges) ─────────────────────
    doc["rpm_limit"]            = (int)Config::rpmLimit;
    doc["tot_limit"]            = Config::totLimit;
    doc["oil_running_min"]      = Config::oilRunningMin;
    // ── EGT rate of rise ──────────────────────────────────────
    doc["tot_rise_rate"]        = (float)(int)(ed.totRiseRate * 10) / 10.0f;
    // ── New expanded sensors ───────────────────────────────────
    doc["has_oil_temp"]         = HardwareConfig::hasOilTemp;
    doc["oil_temp"]             = (float)(int)(ed.oilTemp * 10) / 10.0f;
    doc["oil_temp_healthy"]     = ed.oilTempHealthy;
    doc["max_oil_temp"]         = (float)(int)(ed.maxOilTemp * 10) / 10.0f;
    doc["oil_temp_limit"]       = Config::oilTempLimit;
    doc["has_batt_voltage"]     = HardwareConfig::hasBattVoltage;
    doc["batt_voltage"]         = (float)(int)(ed.battVoltage * 100) / 100.0f;
    doc["batt_healthy"]         = ed.battHealthy;
    doc["max_batt_voltage"]     = (float)(int)(ed.maxBattVoltage * 100) / 100.0f;
    doc["batt_volt_min"]        = Config::battVoltMin;
    doc["has_torque"]           = HardwareConfig::hasTorque;
    doc["torque"]               = (float)(int)(ed.torque * 10) / 10.0f;
    doc["turbo_power_w"]        = (int)ed.turboPower;
    doc["torque_healthy"]       = ed.torqueHealthy;
    doc["has_fuel_press"]       = HardwareConfig::hasFuelPress;
    doc["fuel_press"]           = (float)(int)(ed.fuelPressure * 100) / 100.0f;
    doc["fuel_press_raw"]       = ed.fuelPressRaw;
    doc["fuel_press_healthy"]   = ed.fuelPressHealthy;
    doc["fuel_press_min"]       = Config::fuelPressMin;
    doc["surge_detected"]       = ed.surgeDetected;
    doc["glow_plug_pct"]        = (int)ed.glowPlugPct;
    doc["bleed_valve_open"]     = ed.bleedValveOpen;
    doc["fuel_pump2_demand"]    = (float)(int)(ed.fuelPump2Demand * 1000) / 1000.0f;
    doc["prop_pitch_demand"]    = (float)(int)(ed.propPitchDemand * 1000) / 1000.0f;
    doc["has_governor"]         = HardwareConfig::hasGovernor;
    doc["governor_target_rpm"]  = (int)Config::governorTargetRpm;
    doc["has_glow_plug"]        = HardwareConfig::hasGlowPlug;
    doc["has_bleed_valve"]      = HardwareConfig::hasBleedValve;
    doc["has_prop_pitch"]       = HardwareConfig::hasPropPitch;
    doc["has_fuel_pump2"]       = HardwareConfig::hasFuelPump2;
    doc["has_mavlink"]          = HardwareConfig::hasMAVLink;
    doc["has_tit"]              = HardwareConfig::hasTit;
    doc["has_starter_assist"]   = HardwareConfig::starterAssistEnabled && (HardwareConfig::starterType != 2);
    doc["has_n2"]               = HardwareConfig::hasN2Rpm;
    doc["has_p1"]               = HardwareConfig::hasP1;
    doc["has_p2"]               = HardwareConfig::hasP2;
    doc["has_glow_current"]    = HardwareConfig::hasGlowCurrentSensor;
    doc["glow_current_amps"]   = (float)(int)(ed.glowCurrentAmps * 10) / 10.0f;
    doc["glow_plug_hot"]       = ed.glowPlugHot;
    doc["has_igniter_current"] = HardwareConfig::hasIgniterCurrentSensor;
    doc["igniter_current_amps"]= (float)(int)(ed.igniterCurrentAmps * 10) / 10.0f;
    doc["igniter_coil"]        = HardwareConfig::igniterCoil;
    doc["tit_limit"]            = Config::titLimit;
    // ── Channel labels ────────────────────────────────────────
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
    // ── Sequence validation issues ────────────────────────────
    doc["seq_has_errors"] = ed.seqHasErrors;
    auto issArr = doc["seq_issues"].to<JsonArray>();
    for (int i = 0; i < ed.seqIssueCount; i++) {
        auto obj = issArr.add<JsonObject>();
        obj["block"] = ed.seqIssues[i].blockName;
        obj["msg"]   = ed.seqIssues[i].reason;
        obj["error"] = ed.seqIssues[i].isError;
    }
    // ── General-purpose DI channels ──────────────────────────
    auto diArr = doc["di_channels"].to<JsonArray>();
    for (int i = 0; i < HardwareConfig::MAX_DI; i++) {
        auto ch = diArr.add<JsonObject>();
        ch["state"] = ed.diState[i];
        ch["label"] = HardwareConfig::diCh[i].label[0]
                      ? HardwareConfig::diCh[i].label
                      : (String("DI-") + (i+1)).c_str();
        ch["role"]  = HardwareConfig::diCh[i].role;
        ch["pin"]   = HardwareConfig::diCh[i].pin;
    }
    size_t n = serializeJson(doc, buf, len);
    if (n >= len) Serial.printf("[WebServer] WARNING: telemetry JSON truncated (%u >= %u) — increase buf\n", (unsigned)n, (unsigned)len);
    return n;
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

    _server.on("/generate_204",          HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/fwlink",                HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/hotspot-detect.html",   HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/connecttest.txt",       HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/ncsi.txt",              HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/redirect",              HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });
    _server.on("/canonical.html",        HTTP_GET,  [](AsyncWebServerRequest* req){ req->redirect("http://192.168.4.1/"); });

    // Serve static files from LittleFS — no-cache so the browser always revalidates.
    // On a local network this is fast enough; prevents stale CSS/JS after firmware updates.
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");

    // GET /api/data — live snapshot
    _server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        char buf[3072];
        _buildTelemetry(buf, sizeof(buf));
        req->send(200, "application/json", buf);
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
                if (!ok) Config::save();  // fromJson already saves on success; force-save in-memory on failure path
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
            // Load current config into a document, merge patch on top, re-apply
            JsonDocument current;
            Config::toJson(current);
            for (JsonPair kv : patch.as<JsonObject>()) {
                current[kv.key()] = kv.value();
            }
            Config::fromJson(current);
            Config::save();
            CommandQueue::push({ OTCommand::APPLY_CONFIG });
            FlightRecorder::logConfigChange("config.patch", 0, 0);
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // GET /api/log
    _server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        FlightRecorder::toJson(g_webTxBuf, sizeof(g_webTxBuf));
        req->send(200, "application/json", g_webTxBuf);
    });

    // GET /api/log/csv — flat CSV for spreadsheet analysis
    _server.on("/api/log/csv", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncResponseStream* resp = req->beginResponseStream("text/csv");
        resp->print("t,ev,details\r\n");
        File f = LittleFS.open(FlightRecorder::PATH, "r");
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() == 0 || line[0] != '{') continue;
                JsonDocument doc;
                if (deserializeJson(doc, line)) continue;
                unsigned long t  = doc["t"] | 0UL;
                const char*   ev = doc["ev"] | "";
                // Collect remaining fields as "key=val key=val ..."
                String detail;
                for (JsonPair kv : doc.as<JsonObject>()) {
                    if (strcmp(kv.key().c_str(), "t")  == 0) continue;
                    if (strcmp(kv.key().c_str(), "ev") == 0) continue;
                    if (detail.length()) detail += ' ';
                    detail += kv.key().c_str();
                    detail += '=';
                    detail += kv.value().as<String>();
                }
                char row[256];
                snprintf(row, sizeof(row), "%lu,\"%s\",\"%s\"\r\n", t, ev, detail.c_str());
                resp->print(row);
            }
            f.close();
        }
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
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400); return;
            }
            const char* cmdStr = doc["cmd"] | "";
            OTPacket pkt;
            if      (strcmp(cmdStr, "FUEL_PRIME")     == 0) pkt.cmd = OTCommand::FUEL_PRIME;
            else if (strcmp(cmdStr, "OIL_PRIME")      == 0) pkt.cmd = OTCommand::OIL_PRIME;
            else if (strcmp(cmdStr, "IGN_TEST")       == 0) pkt.cmd = OTCommand::IGN_TEST;
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
            else { req->send(400); return; }
            pkt.fParam = doc["fParam"] | 0.0f;
            pkt.iParam = doc["iParam"] | 0;
            CommandQueue::push(pkt);
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

    // GET /api/session/log — most recent session CSV
    _server.on("/api/session/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        const char* path = SessionLogger::currentPath();
        if (!path || path[0] == '\0' || !LittleFS.exists(path)) {
            req->send(404, "text/plain", "No session log");
            return;
        }
        req->send(LittleFS, path, "text/csv", true);
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
                Serial.printf("[OTA] Upload start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Success: %u bytes — rebooting\n", index + len);
                } else {
                    Update.printError(Serial);
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
            HardwareConfig::toJson(g_webTxBuf, sizeof(g_webTxBuf));
            JsonDocument current;
            deserializeJson(current, g_webTxBuf);
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
            // Apply hardware section
            if (fullDoc[HardwareConfig::SECTION].is<JsonObject>()) {
                JsonDocument hwDoc;
                hwDoc.set(fullDoc[HardwareConfig::SECTION]);
                char tmp[4096]; serializeJson(hwDoc, tmp, sizeof(tmp));
                HardwareConfig::fromJson(tmp, strlen(tmp));
            }
            // Apply settings section
            if (fullDoc[Config::SECTION].is<JsonObject>()) {
                JsonDocument sd;
                sd.set(fullDoc[Config::SECTION]);
                char tmp[4096]; serializeJson(sd, tmp, sizeof(tmp));
                Config::fromJson(tmp, strlen(tmp));
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

    // WebSocket
    _ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type,
                   void* arg, uint8_t* data, size_t len) {
        // Client connections tracked by AsyncWebSocket internally
        (void)type; (void)arg; (void)data; (void)len;
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
    _dns.processNextRequest();   // captive portal — must run every loop

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
    // Skip entirely if no WiFi station connected — saves CPU and JSON serialization
    if (WiFi.softAPgetStationNum() == 0) return;
    // Faster push rate in STANDBY; back off during engine operation to keep ECU as priority
    SysMode m = EngineData::instance().mode;
    uint32_t interval = (m == SysMode::STANDBY) ? 100 : Config::wsIntervalMs;
    unsigned long now = millis();
    if (now - _lastWsMs < interval) return;
    _lastWsMs = now;
    _pushTelemetry();
}

void WebServer::_pushTelemetry() {
    if (_ws.count() == 0) return;
    // Buffer must be large enough for the full telemetry JSON (~3500–4200 bytes).
    // If this is too small the JSON is silently truncated, JSON.parse() throws on
    // the client, and telemetry stops updating entirely.
    char buf[6144];
    _buildTelemetry(buf, sizeof(buf));
    _ws.textAll(buf);
    _ws.cleanupClients();
}
