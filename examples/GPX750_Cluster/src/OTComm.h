#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
//  OTComm.h — OpenTurbine ClusterSerial protocol v2 parser
//
//  Drop-in replacement for EcuComm.h.  Same public API — same field names and
//  types — so main.cpp and BtConfig.h need only trivial type-name changes.
//
//  Protocol overview (what OpenTurbine ECU sends):
//
//  Boot  (sent 3× so the cluster catches it while still initialising):
//    OT:<ver>\n               — protocol version marker → versionReceived = true
//    P:<profile>\n            — profile ID  → populates ecuVersion string
//    M:<code>,<sev>,<label>\n — message table entry (sev: 0=info 1=warn 2=crit)
//    F:<idx>,<key>,<type>,<unit>\n — field definition (idx = column in D: packet)
//    L:<key>=<val>;...\n      — gauge limits / warning thresholds
//    Z\n                      — end of schema → configReceived = true
//    S:<code>\n               — initial status code
//
//  Runtime:
//    D:<v0>,<v1>,...\n        — positional sensor values (order defined by F: lines)
//    S:<code>\n               — status change
//
//  There is no runtime M: overlay in the OT protocol.  overlayActive is always
//  false and overlayText is always empty.
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// ── Status severity constants — used in main.cpp statusColor() ────────────────
static constexpr int INFO     = 0;
static constexpr int WARNING  = 1;
static constexpr int CRITICAL = 2;

class OTComm {
public:

    // ── Live sensor values ────────────────────────────────────────────────────
    float         valN1            = 0.0f;  // N1 in krpm (D: col mapped to N1 / 1000)
    float         valN2            = 0.0f;  // N2 in krpm (D: col mapped to N2 / 1000)
    float         valTot           = 0.0f;  // TOT in °C
    float         valOil           = 0.0f;  // oil pressure in bar

    // ── Status ────────────────────────────────────────────────────────────────
    int           statusCode       = 0;
    int           statusSeverity   = INFO;
    String        statusText       = "---";

    // ── Overlay — OT protocol has no runtime overlay; always inactive ─────────
    bool          overlayActive    = false;
    String        overlayText      = "";

    // ── Config / limits (populated from L: line in boot schema) ──────────────
    float         cfgN1MaxRpm      = 100000.0f;
    float         cfgN1WarnRpm     =  90000.0f;
    float         cfgN2WarnRpm     =  22000.0f;
    float         cfgOilMinWarnBar =      2.2f;
    float         cfgTotMaxC       =    750.0f;
    float         cfgTotWarnC      =    680.0f;

    // ── Protocol state ────────────────────────────────────────────────────────
    bool          versionReceived  = false;  // true after OT: line received
    bool          configReceived   = false;  // true after Z (end-of-schema) received
    bool          dataReceived     = false;  // true after first D: packet received
    unsigned long lastDataTime     = 0;      // millis() of last D: packet
    String        ecuVersion       = "";     // e.g. "OpenTurbine [my_turbine_v1]"

    // ── Constructor ───────────────────────────────────────────────────────────
    explicit OTComm(HardwareSerial& serial) : _serial(serial) {}

    // ── begin() — call once in setup() ───────────────────────────────────────
    // Loads the built-in default message table (matches OT ClusterSerial hard-coded
    // entries) so status text is sensible if the schema arrives after begin() returns.
    void begin() {
        _loadDefaultTable();
        applyStatus(4);  // code 4 = "Ready to start"
    }

    // ── update() — drain serial and parse lines; call every loop() ───────────
    void update() {
        while (_serial.available()) {
            char c = (char)_serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                _line.trim();
                if (_line.length()) _parseLine(_line);
                _line = "";
            } else {
                if (_line.length() < 127) _line += c;
            }
        }
    }

    // ── applyStatus() — look up code in message table and apply ──────────────
    // Called internally on S: lines.  Also called by main.cpp in DEMO_MODE.
    void applyStatus(int code) {
        statusCode = code;
        for (int i = 0; i < _msgCount; i++) {
            if (_msgTable[i].code == (uint8_t)code) {
                statusSeverity = _msgTable[i].sev;
                statusText     = _msgTable[i].label;
                return;
            }
        }
        // Unknown code — numeric fallback
        statusSeverity = INFO;
        statusText     = "S:" + String(code);
    }

// ─────────────────────────────────────────────────────────────────────────────
private:

    HardwareSerial& _serial;
    String          _line;

    // ── Message table ─────────────────────────────────────────────────────────
    static constexpr int MAX_MSG = 32;
    struct MsgEntry {
        uint8_t code;
        uint8_t sev;
        char    label[32];
    };
    MsgEntry _msgTable[MAX_MSG];
    int      _msgCount = 0;

    // ── Field → column slot mapping ───────────────────────────────────────────
    // F:<idx>,<key>,... maps a D: column index to an internal slot:
    //   slot 0 = N1 (rpm)    slot 1 = TOT (°C)   slot 2 = oil pressure (bar)
    //   slot 3 = flame (bool, unused in display)  slot 4 = N2 (rpm)
    static constexpr int MAX_COLS = 8;
    int8_t _colSlot[MAX_COLS];   // -1 = ignore this column
    int    _colCount = 0;        // highest defined column + 1

    bool   _schemaComplete = false;

    // ── Built-in default message table (mirrors OT ClusterSerial defs) ────────
    void _loadDefaultTable() {
        static const struct { uint8_t code; uint8_t sev; const char* label; } defs[] = {
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
        _msgCount = (int)(sizeof(defs) / sizeof(defs[0]));
        for (int i = 0; i < _msgCount; i++) {
            _msgTable[i].code = defs[i].code;
            _msgTable[i].sev  = defs[i].sev;
            strncpy(_msgTable[i].label, defs[i].label, sizeof(_msgTable[i].label) - 1);
            _msgTable[i].label[sizeof(_msgTable[i].label) - 1] = '\0';
        }
        for (int i = 0; i < MAX_COLS; i++) _colSlot[i] = -1;
        _colCount = 0;
    }

    // ── Line dispatcher ───────────────────────────────────────────────────────
    void _parseLine(const String& line) {

        // ── OT:<ver>  — protocol version marker ──────────────────────────────
        if (line.startsWith("OT:")) {
            _msgCount       = 0;
            _colCount       = 0;
            for (int i = 0; i < MAX_COLS; i++) _colSlot[i] = -1;
            _schemaComplete = false;
            versionReceived = true;
            ecuVersion      = "OpenTurbine v" + line.substring(3);
            return;
        }

        // ── P:<profile>  — profile identifier ────────────────────────────────
        if (line.startsWith("P:")) {
            ecuVersion = "OpenTurbine [" + line.substring(2) + "]";
            return;
        }

        // ── M:<code>,<sev>,<label>  — message table entry (schema phase only) ─
        if (line.startsWith("M:") && !_schemaComplete) {
            String rest = line.substring(2);
            int    c1   = rest.indexOf(',');
            int    c2   = rest.indexOf(',', c1 + 1);
            if (c1 < 0 || c2 < 0) return;
            uint8_t code  = (uint8_t)rest.substring(0, c1).toInt();
            uint8_t sev   = (uint8_t)rest.substring(c1 + 1, c2).toInt();
            String  label = rest.substring(c2 + 1);
            if (_msgCount < MAX_MSG) {
                _msgTable[_msgCount].code = code;
                _msgTable[_msgCount].sev  = sev;
                strncpy(_msgTable[_msgCount].label, label.c_str(),
                        sizeof(_msgTable[_msgCount].label) - 1);
                _msgTable[_msgCount].label[sizeof(_msgTable[_msgCount].label) - 1] = '\0';
                _msgCount++;
            }
            return;
        }

        // ── F:<idx>,<key>,<type>,<unit>  — field definition (schema phase only) ─
        if (line.startsWith("F:") && !_schemaComplete) {
            String rest = line.substring(2);
            int    c1   = rest.indexOf(',');
            if (c1 < 0) return;
            int    col  = rest.substring(0, c1).toInt();
            if (col < 0 || col >= MAX_COLS) return;

            String rem  = rest.substring(c1 + 1);
            int    c2   = rem.indexOf(',');
            String key  = (c2 >= 0) ? rem.substring(0, c2) : rem;
            key.trim();

            int8_t slot = -1;
            if      (key == "N1")                       slot = 0;
            else if (key == "T"   || key == "TOT")      slot = 1;
            else if (key == "P"   || key == "OIL")      slot = 2;
            else if (key == "F"   || key == "FLM")      slot = 3;
            else if (key == "N2")                       slot = 4;

            _colSlot[col] = slot;
            if (col >= _colCount) _colCount = col + 1;
            return;
        }

        // ── L:<key>=<val>;...  — gauge limits (schema phase only) ────────────
        if (line.startsWith("L:") && !_schemaComplete) {
            String rest = line.substring(2);
            int    pos  = 0;
            while (pos < (int)rest.length()) {
                int    semi = rest.indexOf(';', pos);
                String pair = (semi < 0) ? rest.substring(pos)
                                         : rest.substring(pos, semi);
                int    eq   = pair.indexOf('=');
                if (eq > 0) {
                    String k = pair.substring(0, eq);
                    float  v = pair.substring(eq + 1).toFloat();
                    if      (k == "N1_MAX")   cfgN1MaxRpm      = v;
                    else if (k == "N1_WARN")  cfgN1WarnRpm     = v;
                    else if (k == "TOT_MAX")  cfgTotMaxC       = v;
                    else if (k == "TOT_WARN") cfgTotWarnC      = v;
                    else if (k == "OIL_WARN") cfgOilMinWarnBar = v;
                    else if (k == "N2_WARN")  cfgN2WarnRpm     = v;
                }
                if (semi < 0) break;
                pos = semi + 1;
            }
            configReceived = true;
            return;
        }

        // ── Z  — end of schema ────────────────────────────────────────────────
        if (line == "Z") {
            _schemaComplete = true;
            // If no F: lines arrived fall back to the OT default column order
            if (_colCount == 0) {
                _colSlot[0] = 0;  // N1
                _colSlot[1] = 1;  // TOT
                _colSlot[2] = 2;  // oil
                _colSlot[3] = 3;  // flame
                _colSlot[4] = 4;  // N2
                _colCount   = 5;
            }
            return;
        }

        // ── S:<code>  — status code ───────────────────────────────────────────
        if (line.startsWith("S:")) {
            applyStatus(line.substring(2).toInt());
            return;
        }

        // ── D:<v0>,<v1>,...  — positional runtime data ────────────────────────
        if (line.startsWith("D:")) {
            String vals = line.substring(2);
            float  col[MAX_COLS];
            int    n   = 0;
            int    pos = 0;
            while (n < MAX_COLS && pos <= (int)vals.length()) {
                int    comma = vals.indexOf(',', pos);
                String tok   = (comma < 0) ? vals.substring(pos)
                                           : vals.substring(pos, comma);
                col[n++] = tok.toFloat();
                if (comma < 0) break;
                pos = comma + 1;
            }
            for (int i = 0; i < n && i < _colCount; i++) {
                switch (_colSlot[i]) {
                    case 0: valN1  = col[i] / 1000.0f; break;  // rpm → krpm
                    case 1: valTot = col[i];            break;
                    case 2: valOil = col[i];            break;
                    case 3: /* flame — not displayed */  break;
                    case 4: valN2  = col[i] / 1000.0f; break;  // rpm → krpm
                    default: break;
                }
            }
            dataReceived = true;
            lastDataTime = millis();
            return;
        }

        // Unknown line — ignored silently
    }
};
