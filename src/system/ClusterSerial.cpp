#include "ClusterSerial.h"
#include "hardware_profile.h"
#include "Config.h"
#include "HardwareConfig.h"
#include <Arduino.h>

// ── Static member definitions ─────────────────────────────────
unsigned long ClusterSerial::_lastDataMs      = 0;
SysMode       ClusterSerial::_lastMode        = SysMode::STANDBY;
uint8_t       ClusterSerial::_lastClusterCode = 0;
bool          ClusterSerial::_totWarnActive   = false;
bool          ClusterSerial::_oilWarnActive   = false;

// Dedicated hardware UART for cluster output (UART2)
static HardwareSerial _port(2);

// ── Message table — M:code,sev,label ─────────────────────────
// sev: 0=info  1=warning  2=critical
struct _MsgDef { uint8_t code; uint8_t sev; const char* label; };
static const _MsgDef _defs[] = {
    {  1, 0, "Running"         },
    {  2, 1, "Relight active"  },
    {  3, 0, "Starting"        },
    {  4, 0, "Ready to start"  },
    {  5, 0, "Igniting"        },
    {  6, 0, "Ignited"         },
    {  7, 2, "Ignition failed" },
    {  8, 0, "Waiting N1"      },
    {  9, 2, "Flame-out"       },
    { 10, 0, "Shutting down"   },
    { 11, 0, "Cooldown"        },
    { 12, 1, "Oil cal invalid" },
    { 13, 1, "Stop switch"     },
    { 14, 2, "Oil pres low"    },
    { 15, 2, "Overspeed"       },
    { 16, 2, "Oil zero/disc"   },
    { 17, 1, "TOT high"        },
    { 18, 1, "Oil warn"        },
};
static constexpr int _numDefs = sizeof(_defs) / sizeof(_defs[0]);

// ── _sendSchema() — full boot schema block ────────────────────
static void _sendSchema() {
    // Protocol version marker — cluster enters schema-parse mode on receipt
    _port.println("OT:1");

    // Profile identifier
    _port.printf("P:%s\n", HardwareConfig::profileId);

    // Message table entries — cluster builds status display from these
    for (int i = 0; i < _numDefs; i++) {
        _port.printf("M:%u,%u,%s\n",
            _defs[i].code, _defs[i].sev, _defs[i].label);
    }

    // Field definitions — idx maps to column position in D: packet
    _port.println("F:0,N1,rpm,RPM");
    _port.println("F:1,T,degC,C");
    _port.println("F:2,P,bar,bar");
    _port.println("F:3,F,bool,");
    if (HardwareConfig::hasN2Rpm) {
        _port.println("F:4,N2,rpm,RPM");
    }

    // Gauge limits / warning thresholds — cluster uses for visual ranges
    _port.printf(
        "L:N1_MAX=%.0f;N1_WARN=%.0f;TOT_MAX=%.0f;TOT_WARN=%.0f;OIL_WARN=%.2f;N2_WARN=%.0f\n",
        Config::rpmLimit,
        Config::n1WarnRpm,
        Config::totLimit,
        Config::totLimit - Config::totSafeMargin,
        Config::oilRunningMin,
        Config::n2WarnRpm
    );

    // End of schema — cluster locks in the table and enters runtime mode
    _port.println("Z");
}

// ── begin() ──────────────────────────────────────────────────
void ClusterSerial::begin() {
    if (!Config::clusterEnabled) return;

    // TX-only: rxPin=-1, txPin=HardwareConfig::clusterTxPin
    uint32_t baud = (uint32_t)HardwareConfig::clusterBaud;
    if (baud == 0) baud = OT_CLUSTER_BAUD;  // fallback to compile-time default
    _port.begin(baud, SERIAL_8N1, -1, HardwareConfig::clusterTxPin);

    // Send schema 3× with gap — cluster may still be booting on first rep
    for (int rep = 0; rep < 3; rep++) {
        _sendSchema();
        delay(100);
    }

    // Initial status
    sendStatus(ClCode::ReadyToStart);
    _lastMode         = SysMode::STANDBY;
    _lastDataMs       = 0;
    _lastClusterCode  = 0;
}

// ── tick() ────────────────────────────────────────────────────
void ClusterSerial::tick() {
    if (!Config::clusterEnabled) return;

    auto& ed = EngineData::instance();
    SysMode m = ed.mode;

    // ── Mode transitions → status code ───────────────────────
    if (m != _lastMode) {
        uint8_t code;
        switch (m) {
            case SysMode::STANDBY:  code = ClCode::ReadyToStart; break;
            case SysMode::STARTUP:  code = ClCode::StartingUp;   break;
            case SysMode::RUNNING:  code = ClCode::Running;       break;
            case SysMode::SHUTDOWN: code = ClCode::ShuttingDown;  break;
            case SysMode::FAULT:    code = ClCode::ShuttingDown;  break;
            default:                code = ClCode::ReadyToStart;  break;
        }
        sendStatus(code);
        _lastMode        = m;
        _lastClusterCode = 0;   // reset so block codes re-fire on next run
    }

    // ── Block-level status codes (set by sequence blocks) ────
    uint8_t cc = ed.clusterCode;
    if (cc != 0 && cc != _lastClusterCode) {
        sendStatus(cc);
        _lastClusterCode = cc;
    }

    // ── Active condition warnings (RUNNING only) ─────────────
    if (m == SysMode::RUNNING) {
        // TOT high warning (approaching limit)
        float totWarnThresh = (Config::totWarnC > 0.0f)
            ? Config::totWarnC
            : (Config::totLimit - Config::totSafeMargin);
        bool totHigh = ed.totHealthy && (ed.tot >= totWarnThresh);
        if (totHigh && !_totWarnActive) {
            _totWarnActive = true;
            sendStatus(ClCode::TotHigh);
        } else if (!totHigh && _totWarnActive) {
            _totWarnActive = false;
            sendStatus(ClCode::Running);  // return to running status
        }

        // Oil pressure warning (running minimum)
        float oilWarnThresh = (Config::oilWarnBar > 0.0f)
            ? Config::oilWarnBar
            : Config::oilRunningMin;
        // Warn slightly above the hard fault threshold (adds 0.3 bar headroom)
        bool oilLow = ed.oilHealthy && (ed.oilPressure < (oilWarnThresh + 0.3f));
        if (oilLow && !_oilWarnActive) {
            _oilWarnActive = true;
            sendStatus(ClCode::OilWarn);
        } else if (!oilLow && _oilWarnActive) {
            _oilWarnActive = false;
            sendStatus(ClCode::Running);
        }
    } else {
        _totWarnActive = false;
        _oilWarnActive = false;
    }

    // ── Periodic sensor data ──────────────────────────────────
    unsigned long now = millis();
    unsigned long interval = (unsigned long)HardwareConfig::clusterIntervalMs;
    if (interval == 0) interval = 50;  // guard against zero
    if (now - _lastDataMs < interval) return;
    _lastDataMs = now;

    // Positional packet: D:N1,T,P,F[,N2]  — no key names at runtime
    char pkt[64];
    int  n = snprintf(pkt, sizeof(pkt), "D:%.0f,%.1f,%.2f,%d",
        (double)ed.n1Rpm, (double)ed.tot,
        (double)ed.oilPressure, ed.flameDetected ? 1 : 0);
    if (n < 0 || n >= (int)sizeof(pkt)) return;
    if (HardwareConfig::hasN2Rpm) {
        int remaining = (int)sizeof(pkt) - n;
        int appended = snprintf(pkt + n, (size_t)remaining, ",%.0f", (double)ed.n2Rpm);
        if (appended < 0 || appended >= remaining) return;
    }
    _port.println(pkt);
}

// ── sendStatus() ─────────────────────────────────────────────
void ClusterSerial::sendStatus(uint8_t code) {
    if (!Config::clusterEnabled) return;
    _port.printf("S:%u\n", code);
}
