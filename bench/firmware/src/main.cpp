// ============================================================
//  OTBench — hardware-in-the-loop tester firmware (classic ESP32)
//
//  A dumb I/O slave for the OpenTurbine bench rig. It is wired pin-to-pin
//  to an ESP32-S3 running OpenTurbine (the DUT). The PC harness drives this
//  board over USB serial and reads back measurements.
//
//  It never contains test logic — it only:
//    - DRIVES the DUT's input pins  (buttons, RPM pulses, analog/threshold sensors)
//    - READS  the DUT's output pins (ESC/pump PWM, relay/solenoid levels)
//
//  Protocol: newline-terminated ASCII, 115200 baud. One reply line per command.
//    PING                 -> OK OTBench <ver>
//    LIST                 -> one "SIG <name> <kind> gpio=<n>" line per signal, then OK
//    RESET                -> all driven outputs to safe/idle, then OK
//    SET <name> <value>   -> drive an output-kind signal, then OK / ERR ...
//                              digital_out_al : value 1=assert(press, LOW), 0=release(Hi-Z)
//                              digital_out    : value !=0 = HIGH (3.3 V), 0 = LOW (0 V)
//                              freq_out       : value = Hz (square wave), 0 = off
//                              dac_out        : value = volts 0..3.3 (true DAC, GPIO25/26)
//    GET <name>           -> read an input-kind signal:
//                              digital_in     : VAL <name> level=<0|1>
//                              pwm_in_*        : VAL <name> us=<high> hz=<f> duty=<d> level=<0|1>
//    STATE                -> VAL STATE <name>=... for every input signal, then OK
//
//  Keep the signal table below in step with bench/pinmap.json.
//  Uses the Arduino-ESP32 3.x LEDC API (ledcAttach / ledcWrite(pin,duty) /
//  ledcWriteTone), matching OpenTurbine's toolchain.
// ============================================================

#include <Arduino.h>
#include <string.h>    // strtok, strcmp, snprintf
#include <strings.h>   // strcasecmp
#include <stdlib.h>    // atoi, atof
#include "soc/gpio_reg.h"  // GPIO_OUT_W1TS_REG / GPIO_IN1_REG for fast ISR pin access

static const char* OTBENCH_VER = "0.3";

// ── Signal kinds ─────────────────────────────────────────────
enum Kind {
    DIGITAL_OUT_AL,   // active-low button: press = drive LOW, release = Hi-Z
    DIGITAL_OUT,      // plain push-pull digital out: !=0 = HIGH, 0 = LOW
    FREQ_OUT,         // square-wave generator (RPM simulation)
    DAC_OUT,          // true DAC analog out (GPIO25/26 only)
    PWM_IN_SERVO,     // capture 50 Hz servo pulse (1000-2000 us)
    PWM_IN_LEDC,      // capture high-frequency LEDC PWM (~10 kHz)
    DIGITAL_IN        // read a relay / solenoid level
};

struct Signal {
    const char* name;
    Kind        kind;
    int         gpio;
};

// ── Signal table — MUST match bench/pinmap.json (tester_gpio / tester_kind) ──
//  Tester board constraints honoured here: GPIO 36/39 unavailable; input-only
//  pins 34-39 and GPIO 16/17 (WROVER PSRAM) avoided; strapping pins avoided.
static Signal SIGNALS[] = {
    // DUT inputs — the tester drives these
    { "START",        DIGITAL_OUT_AL, 13 },
    { "STOP",         DIGITAL_OUT_AL, 14 },
    { "N1",           FREQ_OUT,        4 },
    { "THROTTLE_IN",  DAC_OUT,        25 },   // true DAC
    { "OILP",         DAC_OUT,        26 },   // true DAC
    { "FLAME",        DIGITAL_OUT,    27 },   // threshold sensor -> digital HIGH/LOW
    { "IDLE_IN",      DIGITAL_OUT,    32 },   // no 3rd DAC -> digital extremes (optional)
    // DUT outputs — the tester reads these
    { "THROTTLE_OUT", PWM_IN_SERVO,   18 },
    { "STARTER_OUT",  PWM_IN_SERVO,   19 },
    { "OILPUMP_OUT",  PWM_IN_LEDC,    21 },
    { "FUEL_SOL",     DIGITAL_IN,     22 },
    { "IGNITER",      DIGITAL_IN,     23 },
    { "STARTER_EN",   DIGITAL_IN,     33 },
};
static const int NUM_SIGNALS = sizeof(SIGNALS) / sizeof(SIGNALS[0]);

static const float VREF = 3.3f;

// ── MAX6675 thermocouple emulator (SPI slave) ────────────────
// The DUT (OpenTurbine, Adafruit MAX6675 lib) is the SPI master and bit-bangs
// slowly. We watch its CLK/CS and clock a fake 16-bit word out on MISO:
//   bit15=0 (dummy) | bits14..3 = temp/0.25 (12-bit) | bit2 = open-circuit flag | bits1..0 = 0
// Wire: S3 TOT CLK(GPIO36)->tester 34, CS(GPIO18)->tester 35, MISO(GPIO37)<-tester 16.
#define TOT_SCK_PIN   34   // input  <- S3 TOT CLK
#define TOT_CS_PIN    35   // input  <- S3 TOT CS
#define TOT_MISO_PIN  16   // output -> S3 TOT MISO

static volatile uint16_t g_totWord    = 0xFFFF;  // all-1s = open-circuit until SET
static volatile bool     g_totEnabled = false;
static volatile int      g_totBit     = -1;

static inline void IRAM_ATTR misoWrite(int bit) {
    if (bit) REG_WRITE(GPIO_OUT_W1TS_REG, (1u << TOT_MISO_PIN));   // MISO pin < 32
    else     REG_WRITE(GPIO_OUT_W1TC_REG, (1u << TOT_MISO_PIN));
}
static inline bool IRAM_ATTR csIsLow() {
    return ((REG_READ(GPIO_IN1_REG) >> (TOT_CS_PIN - 32)) & 1) == 0;  // CS pin 32-39
}

static void IRAM_ATTR totCsISR() {
    if (csIsLow()) {                       // selected: present MSB
        g_totBit = 15;
        misoWrite(g_totEnabled ? ((g_totWord >> 15) & 1) : 1);
    } else {                               // deselected: idle high (reads as open)
        g_totBit = -1;
        misoWrite(1);
    }
}
static void IRAM_ATTR totSckISR() {        // advance one bit per rising edge
    if (g_totBit < 0) return;
    g_totBit--;
    if (g_totBit >= 0) misoWrite(g_totEnabled ? ((g_totWord >> g_totBit) & 1) : 1);
}

static void totBegin() {
    pinMode(TOT_SCK_PIN, INPUT);
    pinMode(TOT_CS_PIN, INPUT);
    pinMode(TOT_MISO_PIN, OUTPUT);
    misoWrite(1);
    attachInterrupt(digitalPinToInterrupt(TOT_CS_PIN), totCsISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(TOT_SCK_PIN), totSckISR, RISING);
}

// Set the emulated temperature (°C). open=true asserts the open-circuit flag.
static void totSet(float celsius, bool open) {
    if (open) { g_totWord = (1u << 2); g_totEnabled = true; return; }
    int counts = (int)(celsius / 0.25f + 0.5f);
    if (counts < 0) counts = 0;
    if (counts > 4095) counts = 4095;
    g_totWord = ((uint16_t)counts) << 3;
    g_totEnabled = true;
}

// ── Helpers ──────────────────────────────────────────────────
static const char* kindName(Kind k) {
    switch (k) {
        case DIGITAL_OUT_AL: return "digital_out_al";
        case DIGITAL_OUT:    return "digital_out";
        case FREQ_OUT:       return "freq_out";
        case DAC_OUT:        return "dac_out";
        case PWM_IN_SERVO:   return "pwm_in_servo";
        case PWM_IN_LEDC:    return "pwm_in_ledc";
        case DIGITAL_IN:     return "digital_in";
    }
    return "?";
}
static bool isOutputKind(Kind k) {
    return k == DIGITAL_OUT_AL || k == DIGITAL_OUT || k == FREQ_OUT || k == DAC_OUT;
}

static Signal* findSignal(const char* name) {
    for (int i = 0; i < NUM_SIGNALS; i++)
        if (strcasecmp(name, SIGNALS[i].name) == 0) return &SIGNALS[i];
    return nullptr;
}

// Put one signal into its safe/idle state.
static void safeState(const Signal& s) {
    switch (s.kind) {
        case DIGITAL_OUT_AL:                       // released -> Hi-Z, DUT pull-up holds high
            pinMode(s.gpio, INPUT);
            break;
        case DIGITAL_OUT:                          // defined LOW at boot (no flame / min idle)
            pinMode(s.gpio, OUTPUT);
            digitalWrite(s.gpio, LOW);
            break;
        case FREQ_OUT:
            ledcWriteTone(s.gpio, 0);              // stop tone -> line low
            break;
        case DAC_OUT:
            dacWrite(s.gpio, 0);                   // 0 V
            break;
        case PWM_IN_SERVO:
        case PWM_IN_LEDC:
        case DIGITAL_IN:
            pinMode(s.gpio, INPUT);
            break;
    }
}

static void initSignals() {
    for (int i = 0; i < NUM_SIGNALS; i++) {
        Signal& s = SIGNALS[i];
        if (s.kind == FREQ_OUT)
            ledcAttach(s.gpio, 1000, 10);          // attach; tone set later
        safeState(s);
    }
}

// ── Output application ───────────────────────────────────────
static bool applyOutput(const Signal& s, const char* valStr, String& err) {
    switch (s.kind) {
        case DIGITAL_OUT_AL: {
            int v = atoi(valStr);
            if (v != 0) { pinMode(s.gpio, OUTPUT); digitalWrite(s.gpio, LOW); } // pressed
            else        { pinMode(s.gpio, INPUT); }                              // released (Hi-Z)
            return true;
        }
        case DIGITAL_OUT: {
            float v = atof(valStr);
            pinMode(s.gpio, OUTPUT);
            digitalWrite(s.gpio, v != 0.0f ? HIGH : LOW);
            return true;
        }
        case FREQ_OUT: {
            float hz = atof(valStr);
            if (hz < 1.0f) ledcWriteTone(s.gpio, 0);
            else           ledcWriteTone(s.gpio, (uint32_t)(hz + 0.5f));
            return true;
        }
        case DAC_OUT: {
            float volts = constrain((float)atof(valStr), 0.0f, VREF);
            dacWrite(s.gpio, (int)(volts / VREF * 255.0f + 0.5f));
            return true;
        }
        default:
            err = "not an output signal";
            return false;
    }
}

// ── Measurement ──────────────────────────────────────────────
static void measurePwm(const Signal& s, unsigned long timeoutUs, char* out, size_t outLen) {
    unsigned long high = pulseIn(s.gpio, HIGH, timeoutUs);
    unsigned long low  = pulseIn(s.gpio, LOW,  timeoutUs);
    int level = digitalRead(s.gpio);
    unsigned long period = high + low;
    float hz   = period > 0 ? 1000000.0f / (float)period : 0.0f;
    float duty = period > 0 ? (float)high / (float)period : (level ? 1.0f : 0.0f);
    snprintf(out, outLen, "VAL %s us=%lu hz=%.1f duty=%.3f level=%d",
             s.name, high, hz, duty, level);
}

static void readSignal(const Signal& s, char* out, size_t outLen) {
    switch (s.kind) {
        case DIGITAL_IN:
            snprintf(out, outLen, "VAL %s level=%d", s.name, digitalRead(s.gpio));
            break;
        case PWM_IN_SERVO:
            measurePwm(s, 30000UL, out, outLen);   // 50 Hz -> 20 ms period
            break;
        case PWM_IN_LEDC:
            measurePwm(s, 2500UL, out, outLen);     // ~10 kHz -> 100 us period
            break;
        default:
            snprintf(out, outLen, "ERR %s is not an input signal", s.name);
            break;
    }
}

// Compact single-token field for STATE (no "VAL name " prefix).
static void readField(const Signal& s, char* out, size_t outLen) {
    if (s.kind == DIGITAL_IN) {
        snprintf(out, outLen, "%s=%d", s.name, digitalRead(s.gpio));
    } else {
        unsigned long timeoutUs = (s.kind == PWM_IN_LEDC) ? 2500UL : 30000UL;
        unsigned long high = pulseIn(s.gpio, HIGH, timeoutUs);
        unsigned long low  = pulseIn(s.gpio, LOW,  timeoutUs);
        int level = digitalRead(s.gpio);
        unsigned long period = high + low;
        float duty = period > 0 ? (float)high / (float)period : (level ? 1.0f : 0.0f);
        float hz   = period > 0 ? 1000000.0f / (float)period : 0.0f;
        snprintf(out, outLen, "%s_us=%lu %s_hz=%.1f %s_duty=%.3f %s_level=%d",
                 s.name, high, s.name, hz, s.name, duty, s.name, level);
    }
}

// ── Command dispatch ─────────────────────────────────────────
static void handleLine(char* line) {
    char* cmd = strtok(line, " \t");
    if (!cmd) return;

    if (strcasecmp(cmd, "PING") == 0) {
        Serial.printf("OK OTBench %s\n", OTBENCH_VER);
        return;
    }
    if (strcasecmp(cmd, "LIST") == 0) {
        for (int i = 0; i < NUM_SIGNALS; i++)
            Serial.printf("SIG %s %s gpio=%d\n", SIGNALS[i].name,
                          kindName(SIGNALS[i].kind), SIGNALS[i].gpio);
        Serial.println("OK");
        return;
    }
    if (strcasecmp(cmd, "RESET") == 0) {
        for (int i = 0; i < NUM_SIGNALS; i++) safeState(SIGNALS[i]);
        Serial.println("OK");
        return;
    }
    if (strcasecmp(cmd, "STATE") == 0) {
        char buf[96];
        String out = "VAL STATE";
        for (int i = 0; i < NUM_SIGNALS; i++) {
            if (isOutputKind(SIGNALS[i].kind)) continue;
            readField(SIGNALS[i], buf, sizeof(buf));
            out += " ";
            out += buf;
        }
        Serial.println(out);
        Serial.println("OK");
        return;
    }
    if (strcasecmp(cmd, "SET") == 0) {
        char* name = strtok(nullptr, " \t");
        char* val  = strtok(nullptr, " \t");
        if (!name || !val) { Serial.println("ERR usage: SET <name> <value>"); return; }
        if (strcasecmp(name, "TOT") == 0) {         // MAX6675 thermocouple emulator
            if      (strcasecmp(val, "open") == 0) { totSet(0, true); }
            else if (strcasecmp(val, "off")  == 0) { g_totEnabled = false; }
            else                                    { totSet(atof(val), false); }
            Serial.println("OK");
            return;
        }
        Signal* s = findSignal(name);
        if (!s) { Serial.printf("ERR unknown signal %s\n", name); return; }
        if (!isOutputKind(s->kind)) { Serial.printf("ERR %s is not an output\n", name); return; }
        String err;
        if (applyOutput(*s, val, err)) Serial.println("OK");
        else                          Serial.printf("ERR %s\n", err.c_str());
        return;
    }
    if (strcasecmp(cmd, "GET") == 0) {
        char* name = strtok(nullptr, " \t");
        if (!name) { Serial.println("ERR usage: GET <name>"); return; }
        if (strcasecmp(name, "TOT") == 0) {
            Serial.printf("VAL TOT celsius=%.2f enabled=%d word=0x%04X\n",
                          (g_totWord >> 3) * 0.25f, g_totEnabled ? 1 : 0, g_totWord);
            return;
        }
        Signal* s = findSignal(name);
        if (!s) { Serial.printf("ERR unknown signal %s\n", name); return; }
        if (isOutputKind(s->kind)) { Serial.printf("ERR %s is not an input\n", name); return; }
        char buf[96];
        readSignal(*s, buf, sizeof(buf));
        Serial.println(buf);
        return;
    }
    Serial.printf("ERR unknown command %s\n", cmd);
}

// ── Arduino entry points ─────────────────────────────────────
static char   lineBuf[128];
static size_t lineLen = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    initSignals();
    totBegin();
    Serial.printf("OK OTBench %s ready (%d signals + TOT thermocouple)\n", OTBENCH_VER, NUM_SIGNALS);
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            if (lineLen > 0) handleLine(lineBuf);
            lineLen = 0;
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = c;
        } else {
            lineLen = 0;
            Serial.println("ERR line too long");
        }
    }
}
