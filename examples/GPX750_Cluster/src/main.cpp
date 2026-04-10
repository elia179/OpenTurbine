// ═══════════════════════════════════════════════════════════════════════════════
//  main.cpp — Kawasaki GPX750 R 1988 Instrument Cluster hardware layer
//
//  Owns all display, gauge, LED, and ADC hardware.
//  ECU serial parsing is delegated to OTComm — read ecu.val* for live data.
//  Wi-Fi AP, web config, and OTA are handled by WifiConfig.
//
//  Adapted from JetEcu/GPX750_Cluster for the OpenTurbine ClusterSerial
//  protocol v2.  Bluetooth removed; replaced by Wi-Fi config page at
//  http://192.168.4.1  (or http://cluster.local on mDNS-capable clients).
// ═══════════════════════════════════════════════════════════════════════════════

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <esp_timer.h>

#include "KicConfig.h"
#include "OTComm.h"
#include "WifiConfig.h"

// ── ECU communication + config objects ───────────────────────────────────────
OTComm     ecu(ECU_SERIAL);
WifiConfig wcfg;

// ── TFT display ───────────────────────────────────────────────────────────────
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// ── UI layout (computed in setup) ────────────────────────────────────────────
static int16_t yHeader;
static int16_t yLine[3];
static int16_t yStatus;
static int16_t xLabel;
static int16_t xValue;
static int16_t spaceW;
static const int16_t unitYOffset  = -12;
static const int16_t unitExtra[3] = { 4, 8, 8 };

// ── Display dirty-tracking ────────────────────────────────────────────────────
static String   prevStr[3]   = { "", "", "" };
static uint16_t prevW[3]     = { 0, 0, 0 };
static uint16_t prevColor[3] = { ST77XX_WHITE, ST77XX_WHITE, ST77XX_WHITE };
static String   prevShownStatus = "";
static uint16_t prevShownColor  = ST77XX_WHITE;

// ── Fuel averaging ────────────────────────────────────────────────────────────
static float fuelBuf[FUEL_SAMPLES];
static int   fuelIdx = 0;

// ── Timers ────────────────────────────────────────────────────────────────────
static unsigned long lastFuelSample = 0;
static unsigned long lastScreen     = 0;

// ── Warning LED blink ─────────────────────────────────────────────────────────
static bool          blinkState     = false;
static bool          warnState      = false;
static unsigned long lastWarnToggle = 0;

// ── NO SIGNAL state machine ───────────────────────────────────────────────────
static bool          inNoSignal  = false;
static unsigned long lossStartMs = 0;
static unsigned long goodStartMs = 0;

// ── Boot splash state ─────────────────────────────────────────────────────────
static bool          inBootSplash       = false;
static unsigned long bootStartMs        = 0;
static unsigned long bootEcuShowUntilMs = 0;
static unsigned long bootSweepStartMs   = 0;  // always declared; only used when bootSweep=true

// ── Overlay scroll state ──────────────────────────────────────────────────────
static unsigned long overlayLastScrollMs = 0;
static int           overlayScrollPos    = 0;

// ── RPM square-wave timer ─────────────────────────────────────────────────────
static esp_timer_handle_t rpmTimer;
static int                rpmSqPin = -1;

// ── Prototypes ────────────────────────────────────────────────────────────────
static inline uint16_t statusColor();
void redrawUI();
void updateDisplay();
void driveTempGauge(float pct);
void driveFuelGauge(float pct);
float fuelAverage();
float readFuelPercent();
void startRpmSquareWave(int gpioPin);
void drawBootSplash();

// ═══════════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════════

static inline uint16_t statusColor() {
    if (ecu.statusCode == 1)              return ST77XX_GREEN;
    if (ecu.statusSeverity == INFO)       return ST77XX_WHITE;
    if (ecu.statusSeverity == WARNING)    return ST77XX_YELLOW;
    if (ecu.statusSeverity == CRITICAL)   return ST77XX_RED;
    return ST77XX_WHITE;
}

void drawBootSplash() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(0, 40);
    tft.print(ecu.ecuVersion.length() ? ecu.ecuVersion : "ECU VERSION: ---");
    tft.setCursor(0, 70);
    tft.print(CLUSTER_VERSION);
    tft.setFont();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);

    // ── Wi-Fi AP, web config, OTA — start early so AP is ready during splash ─
    wcfg.begin();

    // ── ECU serial ────────────────────────────────────────────────────────────
    ECU_SERIAL.setRxBufferSize(ECU_RX_BUF);
    ECU_SERIAL.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);
    ECU_SERIAL.setTimeout(50);

    // ── TFT ───────────────────────────────────────────────────────────────────
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.initR(INITR_BLACKTAB);
    tft.setSPISpeed(8000000);
    tft.setRotation(1);

    // ── Output pins ───────────────────────────────────────────────────────────
    pinMode(TEMP_GAUGE_PIN,    OUTPUT);
    pinMode(FUEL_GAUGE_PIN,    OUTPUT);
    pinMode(FUEL_LIGHT_PIN,    OUTPUT);
    pinMode(WARNING_LIGHT_PIN, OUTPUT);
    pinMode(FUEL_SENSOR_PIN,   INPUT);

    // ADC — 12-bit default in ESP32 Arduino 3.x
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(FUEL_SENSOR_PIN, ADC_11db);

    // ── Measure space width for unit label placement ──────────────────────────
    int16_t bx, by; uint16_t bw, bh;
    tft.setFont();
    tft.setTextSize(1);
    tft.getTextBounds(" ", 0, 0, &bx, &by, &bw, &bh);
    spaceW = (int16_t)bw;

    // ── Compute xValue from widest label ──────────────────────────────────────
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    uint16_t maxW = 0;
    xLabel  = 0;
    yHeader = 16;
    for (int i = 0; i < 3; ++i) {
        yLine[i] = 40 + i * 24;
        tft.getTextBounds(LABELS[i], 0, 0, &bx, &by, &bw, &bh);
        if (bw > maxW) maxW = bw;
    }
    xValue = xLabel + (int16_t)maxW + spaceW + 4;

    // ── Fuel gauge init ───────────────────────────────────────────────────────
    float initPct = readFuelPercent();
    for (int i = 0; i < FUEL_SAMPLES; ++i) fuelBuf[i] = initPct;
    fuelIdx = 0;
    driveFuelGauge(initPct);

    // ── RPM square-wave ───────────────────────────────────────────────────────
    startRpmSquareWave(RPM_GAUGE_PIN);

    // ── OTComm init ───────────────────────────────────────────────────────────
    ecu.begin();
    ecu.lastDataTime = millis();  // grace period on boot

    // Push factory defaults into ecu.cfg* (ECU will override via L: line)
    wcfg.apply(ecu);

    lastScreen = millis();

    // ── Boot splash or demo mode ──────────────────────────────────────────────
    if (wcfg.cfg.demoMode) {
        inBootSplash = false;
        redrawUI();
    } else {
        bootStartMs        = millis();
        bootEcuShowUntilMs = 0;
        inBootSplash       = true;
        if (wcfg.cfg.bootSweep) bootSweepStartMs = bootStartMs;
        drawBootSplash();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Main loop
// ═══════════════════════════════════════════════════════════════════════════════

void loop() {
    const unsigned long now = millis();

    // ── Apply pending config changes from web UI ──────────────────────────────
    // Web server runs on Core 0; configDirty is our handshake flag.
    if (wcfg.configDirty) {
        wcfg.configDirty = false;
        wcfg.apply(ecu);
        if (!inBootSplash && !inNoSignal) redrawUI();
    }

    // ── Boot splash ───────────────────────────────────────────────────────────
    if (!wcfg.cfg.demoMode && inBootSplash) {
        ecu.update();

        // Gauge sweep during splash until first real data arrives
        if (wcfg.cfg.bootSweep && !ecu.dataReceived) {
            const uint32_t t = now - bootSweepStartMs;
            float x = 0.0f;
            if      (t < 1000UL) x = float(t) / 1000.0f;
            else if (t < 2000UL) x = 1.0f;
            ecu.valN1  = (x * ecu.cfgN1MaxRpm) / 1000.0f;
            ecu.valTot = x * ecu.cfgTotMaxC;
            driveTempGauge(x * 100.0f);
        }

        // OT:1 received — show splash for BOOT_SHOW_ECU_MS then switch to UI
        if (ecu.versionReceived && bootEcuShowUntilMs == 0) {
            bootEcuShowUntilMs = now + BOOT_SHOW_ECU_MS;
            drawBootSplash();  // redraw with actual profile string
        }
        if (bootEcuShowUntilMs != 0) {
            if ((long)(bootEcuShowUntilMs - now) > 0) return;
            inBootSplash = false;
            redrawUI();
            return;
        }

        // Timeout waiting for OT:1 — proceed anyway
        if (now - bootStartMs >= BOOT_WAIT_ECU_MS) {
            inBootSplash = false;
            redrawUI();
        }
        return;
    }

    // ── ECU data / demo sweep ─────────────────────────────────────────────────
    if (wcfg.cfg.demoMode) {
        static unsigned long lastStatusChange = 0;
        const unsigned long c = now % 30000UL;
        if (c < 25000UL) {
            float f    = float(c) / 25000.0f;
            ecu.valOil = f * 5.0f;
            ecu.valTot = f * ecu.cfgTotMaxC;
            ecu.valN2  = f * 50.0f;
            ecu.valN1  = f * (ecu.cfgN1MaxRpm / 1000.0f);
            if (now - lastStatusChange >= 5000UL) {
                ecu.applyStatus(1 + random(14));
                lastStatusChange = now;
            }
            ecu.lastDataTime = now;
        }
    } else {
        ecu.update();
    }

    // ── 1 Hz fuel sampling ────────────────────────────────────────────────────
    if (now - lastFuelSample >= 1000UL) {
        lastFuelSample = now;
        float pct = wcfg.cfg.demoMode
            ? float(now % 25000UL) / 250.0f
            : readFuelPercent();
        fuelBuf[fuelIdx++] = pct;
        if (fuelIdx >= FUEL_SAMPLES) fuelIdx = 0;
    }

    // ── NO SIGNAL state machine ───────────────────────────────────────────────
    const bool recentData = (now - ecu.lastDataTime) <= ENTER_LOSS_MS;

    if (!inNoSignal) {
        if (!recentData) {
            if (lossStartMs == 0) lossStartMs = now;
            if ((now - lossStartMs) >= wcfg.cfg.enterLossMs) {
                inNoSignal  = true;
                lossStartMs = 0;
                tft.fillScreen(ST77XX_BLACK);
                tft.setFont(&FreeSansBold9pt7b);
                tft.setTextSize(1);
                tft.setTextColor(ST77XX_YELLOW);
                String msg = "NO SIGNAL";
                int16_t bx, by; uint16_t bw, bh;
                tft.getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
                tft.setCursor((tft.width() - bw) / 2, (tft.height() - bh) / 2);
                tft.print(msg);
                return;
            }
        } else {
            lossStartMs = 0;
        }
    } else {
        if (recentData) {
            if (goodStartMs == 0) goodStartMs = now;
            if ((now - goodStartMs) >= wcfg.cfg.exitGoodMs) {
                inNoSignal  = false;
                goodStartMs = 0;
                redrawUI();
            } else { return; }
        } else {
            goodStartMs = 0;
            return;
        }
    }

    // ── Gauges and indicators ─────────────────────────────────────────────────
    driveTempGauge((ecu.valTot / ecu.cfgTotMaxC) * 100.0f);
    float fuelPct = fuelAverage();
    driveFuelGauge(fuelPct);
    digitalWrite(FUEL_LIGHT_PIN, fuelPct < wcfg.cfg.fuelWarnPct ? HIGH : LOW);

    // ── Warning LED blink ─────────────────────────────────────────────────────
    if (now - lastWarnToggle >= 500UL) {
        warnState      = !warnState;
        lastWarnToggle = now;
    }
    bool anyGaugeWarn =
        (ecu.valOil < ecu.cfgOilMinWarnBar)          ||
        (ecu.valTot > ecu.cfgTotWarnC)               ||
        (ecu.valN2 * 1000.0f > ecu.cfgN2WarnRpm)     ||
        (ecu.valN1 * 1000.0f > ecu.cfgN1WarnRpm);

    bool lightOn = (ecu.statusSeverity == CRITICAL) || anyGaugeWarn;
    digitalWrite(WARNING_LIGHT_PIN, (lightOn && warnState) ? HIGH : LOW);

    // ── UI refresh @ 4 Hz ─────────────────────────────────────────────────────
    if (now - lastScreen >= 250UL) {
        lastScreen += 250UL;
        blinkState = !blinkState;
        updateDisplay();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Display
// ═══════════════════════════════════════════════════════════════════════════════

void redrawUI() {
    tft.fillScreen(ST77XX_BLACK);

    const char* hdrText = wcfg.cfg.rpmDisplayPercent ? "% max N1" : "x10 000r/min";
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(hdrText, 0, 0, &bx, &by, &bw, &bh);
    tft.setCursor((tft.width() - bw) / 2, yHeader);
    tft.print(hdrText);

    char buf[8];
    for (int i = 0; i < 3; ++i) {
        int16_t y = yLine[i];
        tft.setFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(xLabel, y);
        tft.print(LABELS[i]);

        if (i == 1) sprintf(buf, "0");
        else        dtostrf(0.0f, 0, 1, buf);

        tft.setCursor(xValue, y);
        tft.print(buf);
        tft.getTextBounds(buf, xValue, y, &bx, &by, &bw, &bh);
        prevW[i]     = bw;
        prevStr[i]   = buf;
        prevColor[i] = ST77XX_WHITE;

        tft.setFont();
        tft.setCursor(xValue + bw + spaceW + unitExtra[i], y + unitYOffset);
        tft.print(UNITS[i]);
    }

    yStatus = tft.height() - FreeSansBold9pt7b.yAdvance + 10;
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(statusColor());
    tft.setCursor(xLabel, yStatus);
    tft.print(ecu.statusText);
    prevShownStatus = ecu.statusText;
    prevShownColor  = statusColor();

    overlayScrollPos    = 0;
    overlayLastScrollMs = millis();
}

void updateDisplay() {
    int16_t bx, by; uint16_t bw, bh;

    float vals[3] = { ecu.valOil, ecu.valTot, ecu.valN2 };
    bool  warns[3] = {
        ecu.valOil < ecu.cfgOilMinWarnBar,
        ecu.valTot > ecu.cfgTotWarnC,
        ecu.valN2 * 1000.0f > ecu.cfgN2WarnRpm,
    };

    auto fmtVal = [&](int i, char* buf) {
        if (i == 1) sprintf(buf, "%d", (int)vals[i]);
        else        dtostrf(vals[i], 0, 1, buf);
    };

    for (int i = 0; i < 3; ++i) {
        int16_t  y   = yLine[i];
        uint16_t col = (warns[i] && blinkState) ? ST77XX_RED : ST77XX_WHITE;

        if (col != prevColor[i]) {
            tft.fillRect(0, y - 14, tft.width(), 16, ST77XX_BLACK);
            tft.setFont(&FreeSansBold9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(col);
            tft.setCursor(xLabel, y);
            tft.print(LABELS[i]);

            char buf[8]; fmtVal(i, buf);
            tft.setCursor(xValue, y);
            tft.print(buf);
            tft.getTextBounds(buf, xValue, y, &bx, &by, &bw, &bh);
            tft.setFont();
            tft.setCursor(xValue + bw + spaceW + unitExtra[i], y + unitYOffset);
            tft.print(UNITS[i]);

            prevStr[i]   = buf;
            prevW[i]     = bw;
            prevColor[i] = col;
        } else {
            char buf[8]; fmtVal(i, buf);
            tft.getTextBounds(buf, xValue, y, &bx, &by, &bw, &bh);
            uint16_t wNew = bw;

            if (String(buf) != prevStr[i] || wNew != prevW[i]) {
                uint16_t clearW = max(prevW[i], wNew) + spaceW + unitExtra[i] + bw;
                tft.fillRect(xValue, y - 14, clearW, 16, ST77XX_BLACK);
                tft.setFont(&FreeSansBold9pt7b);
                tft.setTextSize(1);
                tft.setTextColor(col);
                tft.setCursor(xValue, y);
                tft.print(buf);
                tft.setFont();
                tft.setCursor(xValue + wNew + spaceW + unitExtra[i], y + unitYOffset);
                tft.print(UNITS[i]);
                prevStr[i] = buf;
                prevW[i]   = wNew;
            }
        }
    }

    // Status line (overlay takes over if active — always false for OT protocol)
    unsigned long now = millis();
    String   shown = ecu.statusText;
    uint16_t col   = statusColor();

    if (ecu.overlayActive) {
        col = ST77XX_RED;
        if (ecu.overlayText.length() <= (size_t)OVERLAY_VISIBLE_CHARS) {
            shown = ecu.overlayText;
        } else {
            if (now - overlayLastScrollMs >= OVERLAY_SCROLL_MS) {
                overlayLastScrollMs += OVERLAY_SCROLL_MS;
                overlayScrollPos++;
            }
            String loop = ecu.overlayText;
            for (int g = 0; g < OVERLAY_GAP_CHARS; ++g) loop += ' ';
            loop += ecu.overlayText;
            for (int g = 0; g < OVERLAY_GAP_CHARS; ++g) loop += ' ';

            int L     = loop.length();
            int start = overlayScrollPos % L;
            if (start + OVERLAY_VISIBLE_CHARS <= L) {
                shown = loop.substring(start, start + OVERLAY_VISIBLE_CHARS);
            } else {
                shown = loop.substring(start) +
                        loop.substring(0, (start + OVERLAY_VISIBLE_CHARS) - L);
            }
        }
    }

    if (shown != prevShownStatus || col != prevShownColor) {
        int16_t fh = FreeSansBold9pt7b.yAdvance;
        tft.fillRect(0, yStatus - fh - 1, tft.width(), fh + 6, ST77XX_BLACK);
        tft.setFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(col);
        tft.setCursor(xLabel, yStatus);
        tft.print(shown);
        prevShownStatus = shown;
        prevShownColor  = col;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Analog gauges
// ═══════════════════════════════════════════════════════════════════════════════

void driveTempGauge(float pct) {
    pct = constrain(pct, 0.0f, 100.0f);
    int o = wcfg.cfg.tempGaugePwm[0];
    for (int i = 0; i < 4; ++i) {
        if (pct >= TEMP_GAUGE_PCT[i] && pct <= TEMP_GAUGE_PCT[i + 1]) {
            float f = (pct - TEMP_GAUGE_PCT[i]) / (TEMP_GAUGE_PCT[i + 1] - TEMP_GAUGE_PCT[i]);
            o = wcfg.cfg.tempGaugePwm[i] +
                (int)((wcfg.cfg.tempGaugePwm[i + 1] - wcfg.cfg.tempGaugePwm[i]) * f + 0.5f);
            break;
        }
    }
    analogWrite(TEMP_GAUGE_PIN, o);
}

void driveFuelGauge(float pct) {
    pct = constrain(pct, 0.0f, 100.0f);
    int o = wcfg.cfg.fuelGaugePwm[0];
    for (int i = 0; i < 4; ++i) {
        if (pct >= float(FUEL_GAUGE_PCT[i]) && pct <= float(FUEL_GAUGE_PCT[i + 1])) {
            float f = (pct - float(FUEL_GAUGE_PCT[i])) /
                      float(FUEL_GAUGE_PCT[i + 1] - FUEL_GAUGE_PCT[i]);
            o = wcfg.cfg.fuelGaugePwm[i] +
                (int)((wcfg.cfg.fuelGaugePwm[i + 1] - wcfg.cfg.fuelGaugePwm[i]) * f + 0.5f);
            break;
        }
    }
    analogWrite(FUEL_GAUGE_PIN, o);
}

float fuelAverage() {
    float s = 0.0f;
    for (int i = 0; i < FUEL_SAMPLES; ++i) s += fuelBuf[i];
    return s / FUEL_SAMPLES;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Fuel ADC
// ═══════════════════════════════════════════════════════════════════════════════

float readFuelPercent() {
    int raw = analogRead(FUEL_SENSOR_PIN);
    raw = constrain(raw, 0, 4095);

    const int a  = wcfg.cfg.fuelRawEmpty;
    const int b  = wcfg.cfg.fuelRawFull;
    const int lo = min(a, b);
    const int hi = max(a, b);
    if (lo == hi) return 0.0f;
    raw = constrain(raw, lo, hi);

    float pct = 100.0f * float(raw - a) / float(b - a);
    return constrain(pct, 0.0f, 100.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  RPM square-wave output (N1 → tachometer gauge)
// ═══════════════════════════════════════════════════════════════════════════════

void startRpmSquareWave(int gpioPin) {
    rpmSqPin = gpioPin;
    pinMode(rpmSqPin, OUTPUT);
    digitalWrite(rpmSqPin, LOW);

    const esp_timer_create_args_t args = {
        .callback = +[](void*) {
            float freq;
            if (wcfg.cfg.calTachoActive) {
                freq = wcfg.cfg.calTachoHz;
            } else {
                float rpm  = ecu.valN1 * 1000.0f;
                float maxR = ecu.cfgN1MaxRpm;
                rpm        = constrain(rpm, 0.0f, maxR);
                freq = (rpm / maxR) * wcfg.cfg.rpmGaugeMaxHz;
            }
            static bool pinHigh = false;

            if (freq < 12.0f) {
                if (pinHigh) { pinHigh = false; digitalWrite(rpmSqPin, LOW); }
                esp_timer_start_once(rpmTimer, 100000);
            } else {
                uint32_t hp_us = (uint32_t)((1.0f / freq) * 1e6f / 2.0f);
                if (hp_us < 2) hp_us = 2;
                pinHigh = !pinHigh;
                digitalWrite(rpmSqPin, pinHigh ? HIGH : LOW);
                esp_timer_start_once(rpmTimer, hp_us);
            }
        },
        .arg  = nullptr,
        .name = "rpm_sq",
    };

    esp_timer_create(&args, &rpmTimer);
    esp_timer_start_once(rpmTimer, 1);
}
