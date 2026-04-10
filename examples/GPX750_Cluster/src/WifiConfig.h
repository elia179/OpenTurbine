#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
//  WifiConfig.h — Cluster Wi-Fi AP + web config + OTA
//
//  Replaces BtConfig.h.  No Bluetooth required.
//
//  On power-up the cluster creates a Wi-Fi access point (default "GPX750-Cluster",
//  no password).  Connect a phone/laptop and open http://192.168.4.1 or
//  http://cluster.local to reach the config page.
//
//  Config is persisted to LittleFS as /cluster_cfg.json.
//  Changes take effect immediately (gauge limits, PWM tables, thresholds).
//  demo_mode and boot_sweep take effect on the next reboot.
//
//  Endpoints:
//    GET  /             → config page (HTML, served from flash)
//    GET  /api/config   → current config as JSON
//    POST /api/config   → save config JSON, responds {ok:true}
//    POST /reboot       → ESP.restart()
//    POST /update       → OTA multipart firmware upload
// ═══════════════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

#include "KicConfig.h"
#include "OTComm.h"
#include "config_html.h"

// ── All runtime-tunable parameters ───────────────────────────────────────────
struct RuntimeCfg {
    // Display
    uint8_t  rpmDisplayPercent = RPM_DISPLAY_PERCENT;
    bool     demoMode          = DEMO_MODE_DEFAULT;
    bool     bootSweep         = BOOT_SWEEP_DEFAULT;

    // Engine limits (cluster defaults — ECU overrides via L: line on connect)
    float    n1MaxRpm          = N1_MAX_RPM_DEFAULT;
    float    n1WarnRpm         = N1_WARN_RPM_DEFAULT;
    float    n2WarnRpm         = N2_WARN_RPM_DEFAULT;
    float    oilMinWarnBar     = OIL_WARN_BAR_DEFAULT;
    float    totMaxC           = TOT_MAX_C_DEFAULT;
    float    totWarnC          = TOT_WARN_C_DEFAULT;
    float    fuelWarnPct       = FUEL_WARN_PCT;

    // Signal-loss timing
    uint32_t enterLossMs       = ENTER_LOSS_MS;
    uint32_t exitGoodMs        = EXIT_GOOD_MS;

    // Tachometer
    float    rpmGaugeMaxHz     = RPM_GAUGE_MAX_HZ;

    // Gauge PWM cal tables (5-point piecewise-linear, breakpoints 0/25/50/75/100%)
    int      tempGaugePwm[5]   = { TEMP_GAUGE_PWM[0], TEMP_GAUGE_PWM[1],
                                   TEMP_GAUGE_PWM[2], TEMP_GAUGE_PWM[3],
                                   TEMP_GAUGE_PWM[4] };
    int      fuelGaugePwm[5]   = { FUEL_GAUGE_PWM[0], FUEL_GAUGE_PWM[1],
                                   FUEL_GAUGE_PWM[2], FUEL_GAUGE_PWM[3],
                                   FUEL_GAUGE_PWM[4] };

    // Fuel sender ADC limits
    int      fuelRawEmpty      = FUEL_RAW_EMPTY;
    int      fuelRawFull       = FUEL_RAW_FULL;

    // Tacho calibration live state — not persisted, reset each boot
    bool     calTachoActive    = false;
    float    calTachoHz        = 100.0f;
};

// ═══════════════════════════════════════════════════════════════════════════════
class WifiConfig {
public:

    RuntimeCfg       cfg;
    volatile bool    configDirty = false;  // set by web callback, cleared by main loop

    // ── begin() — call once early in setup() ─────────────────────────────────
    void begin() {
        // LittleFS
        if (!LittleFS.begin(true)) {
            Serial.println("[WifiConfig] LittleFS mount failed — formatting");
        }
        _load();

        // Access point
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS[0] ? AP_PASS : nullptr);
        Serial.printf("[WifiConfig] AP  SSID='%s'  IP=%s\n",
            AP_SSID, WiFi.softAPIP().toString().c_str());

        // mDNS  →  http://cluster.local
        if (MDNS.begin(AP_MDNS_HOST)) {
            Serial.printf("[WifiConfig] mDNS  http://%s.local\n", AP_MDNS_HOST);
        }

        _setupRoutes();
        _server.begin();
        Serial.println("[WifiConfig] Web server started");
    }

    // ── apply() — push limits into the OTComm object; call after configDirty ─
    void apply(OTComm& ecu) const {
        ecu.cfgN1MaxRpm      = cfg.n1MaxRpm;
        ecu.cfgN1WarnRpm     = cfg.n1WarnRpm;
        ecu.cfgN2WarnRpm     = cfg.n2WarnRpm;
        ecu.cfgOilMinWarnBar = cfg.oilMinWarnBar;
        ecu.cfgTotMaxC       = cfg.totMaxC;
        ecu.cfgTotWarnC      = cfg.totWarnC;
    }

// ─────────────────────────────────────────────────────────────────────────────
private:

    static constexpr const char* _CONFIG_PATH = "/cluster_cfg.json";

    AsyncWebServer _server{80};
    String         _postBody;   // accumulates POST /api/config body across chunks

    // ── Persist ───────────────────────────────────────────────────────────────

    void _load() {
        File f = LittleFS.open(_CONFIG_PATH, "r");
        if (!f) {
            Serial.println("[WifiConfig] No saved config — using defaults");
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, f) != DeserializationError::Ok) {
            Serial.println("[WifiConfig] Config parse error — using defaults");
            f.close(); return;
        }
        f.close();
        _fromDoc(doc);
        Serial.println("[WifiConfig] Config loaded");
    }

    void _save() {
        File f = LittleFS.open(_CONFIG_PATH, "w");
        if (!f) { Serial.println("[WifiConfig] Config save failed — LittleFS error"); return; }
        JsonDocument doc;
        _toDoc(doc);
        serializeJson(doc, f);
        f.close();
        Serial.println("[WifiConfig] Config saved");
    }

    // ── JSON ↔ cfg ─────────────────────────────────────────────────────────────

    void _toDoc(JsonDocument& doc) const {
        doc["rpm_mode"]       = cfg.rpmDisplayPercent;
        doc["demo_mode"]      = cfg.demoMode;
        doc["boot_sweep"]     = cfg.bootSweep;
        doc["n1_max"]         = cfg.n1MaxRpm;
        doc["n1_warn"]        = cfg.n1WarnRpm;
        doc["n2_warn"]        = cfg.n2WarnRpm;
        doc["tot_max"]        = cfg.totMaxC;
        doc["tot_warn"]       = cfg.totWarnC;
        doc["oil_warn"]       = cfg.oilMinWarnBar;
        doc["fuel_warn"]      = cfg.fuelWarnPct;
        doc["enter_loss_ms"]  = cfg.enterLossMs;
        doc["exit_good_ms"]   = cfg.exitGoodMs;
        doc["rpm_max_hz"]     = cfg.rpmGaugeMaxHz;
        doc["fuel_raw_empty"] = cfg.fuelRawEmpty;
        doc["fuel_raw_full"]  = cfg.fuelRawFull;

        JsonArray tp = doc["temp_pwm"].to<JsonArray>();
        JsonArray fp = doc["fuel_pwm"].to<JsonArray>();
        for (int i = 0; i < 5; i++) { tp.add(cfg.tempGaugePwm[i]); fp.add(cfg.fuelGaugePwm[i]); }
    }

    void _fromDoc(const JsonDocument& doc) {
        cfg.rpmDisplayPercent = doc["rpm_mode"]      | cfg.rpmDisplayPercent;
        cfg.demoMode          = doc["demo_mode"]     | cfg.demoMode;
        cfg.bootSweep         = doc["boot_sweep"]    | cfg.bootSweep;
        cfg.n1MaxRpm          = doc["n1_max"]        | cfg.n1MaxRpm;
        cfg.n1WarnRpm         = doc["n1_warn"]       | cfg.n1WarnRpm;
        cfg.n2WarnRpm         = doc["n2_warn"]       | cfg.n2WarnRpm;
        cfg.totMaxC           = doc["tot_max"]       | cfg.totMaxC;
        cfg.totWarnC          = doc["tot_warn"]      | cfg.totWarnC;
        cfg.oilMinWarnBar     = doc["oil_warn"]      | cfg.oilMinWarnBar;
        cfg.fuelWarnPct       = doc["fuel_warn"]     | cfg.fuelWarnPct;
        cfg.enterLossMs       = doc["enter_loss_ms"] | cfg.enterLossMs;
        cfg.exitGoodMs        = doc["exit_good_ms"]  | cfg.exitGoodMs;
        cfg.rpmGaugeMaxHz     = doc["rpm_max_hz"]    | cfg.rpmGaugeMaxHz;
        cfg.fuelRawEmpty      = doc["fuel_raw_empty"]| cfg.fuelRawEmpty;
        cfg.fuelRawFull       = doc["fuel_raw_full"] | cfg.fuelRawFull;

        if (doc["temp_pwm"].is<JsonArrayConst>()) {
            JsonArrayConst a = doc["temp_pwm"].as<JsonArrayConst>();
            for (int i = 0; i < 5 && i < (int)a.size(); i++) cfg.tempGaugePwm[i] = a[i];
        }
        if (doc["fuel_pwm"].is<JsonArrayConst>()) {
            JsonArrayConst a = doc["fuel_pwm"].as<JsonArrayConst>();
            for (int i = 0; i < 5 && i < (int)a.size(); i++) cfg.fuelGaugePwm[i] = a[i];
        }
    }

    // ── Routes ────────────────────────────────────────────────────────────────

    void _setupRoutes() {

        // ── GET /  — config page ──────────────────────────────────────────────
        _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "text/html", CONFIG_HTML);
        });

        // ── GET /api/config  — current config as JSON ─────────────────────────
        _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* req) {
            JsonDocument doc;
            _toDoc(doc);
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── POST /api/config  — save config ───────────────────────────────────
        // ESPAsyncWebServer delivers body via the onBody (4th) handler.
        // We accumulate chunks and process when the last one arrives.
        _server.on("/api/config", HTTP_POST,
            // request complete handler (body already processed by onBody)
            [](AsyncWebServerRequest* req) {},
            // upload handler (unused)
            nullptr,
            // onBody handler
            [this](AsyncWebServerRequest* req,
                   uint8_t* data, size_t len, size_t index, size_t total) {
                if (index == 0) _postBody = "";
                _postBody += String(reinterpret_cast<const char*>(data), len);
                if (index + len < total) return;  // wait for last chunk

                JsonDocument doc;
                if (deserializeJson(doc, _postBody) != DeserializationError::Ok) {
                    req->send(400, "application/json", R"({"ok":false,"err":"parse"})");
                    return;
                }
                _fromDoc(doc);
                _save();
                configDirty = true;
                req->send(200, "application/json", R"({"ok":true})");
            }
        );

        // ── POST /reboot  — soft restart ──────────────────────────────────────
        _server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
            req->send(200, "text/plain", "rebooting");
            delay(200);
            ESP.restart();
        });

        // ── POST /update  — OTA firmware upload ───────────────────────────────
        _server.on("/update", HTTP_POST,
            // completion handler — called after all upload chunks delivered
            [](AsyncWebServerRequest* req) {
                bool ok = !Update.hasError();
                req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
                if (ok) { delay(200); ESP.restart(); }
            },
            // upload handler — called for each multipart chunk
            [](AsyncWebServerRequest* req,
               const String& filename, size_t index, uint8_t* data,
               size_t len, bool final) {
                if (!index) {
                    Serial.printf("[OTA] Start: %s  size=%u\n",
                        filename.c_str(), req->contentLength());
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                        Update.printError(Serial);
                    }
                }
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
                if (final) {
                    if (Update.end(true)) {
                        Serial.printf("[OTA] Done: %u bytes\n", index + len);
                    } else {
                        Update.printError(Serial);
                    }
                }
            }
        );

        // ── 404 ───────────────────────────────────────────────────────────────
        _server.onNotFound([](AsyncWebServerRequest* req) {
            req->send(404, "text/plain", "not found");
        });
    }
};
