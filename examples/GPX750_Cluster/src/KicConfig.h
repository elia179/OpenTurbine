#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
//  KicConfig.h — Compile-time constants for the GPX750 R 1988 Instrument Cluster
//
//  This file sets hardware pin assignments, factory defaults for all runtime
//  parameters, and the Wi-Fi AP identity.
//
//  All runtime parameters (limits, gauge cal, display mode, demo mode …) are
//  also editable at http://cluster.local after first boot — you do not need to
//  re-flash the cluster to change them.
// ═══════════════════════════════════════════════════════════════════════════════

// ── Firmware version shown at boot ────────────────────────────────────────────
static const char* CLUSTER_VERSION = "GPX750 v0.4";

// ── Wi-Fi access point ────────────────────────────────────────────────────────
// Connect to this SSID then open http://192.168.4.1 or http://cluster.local
static constexpr const char* AP_SSID      = "GPX750-Cluster";
static constexpr const char* AP_PASS      = "";          // "" = open (no password)
static constexpr const char* AP_MDNS_HOST = "cluster";   // → http://cluster.local

// ── TFT display pins (ESP32 hardware SPI) ─────────────────────────────────────
// ESP32 defaults below. For ESP32-S3 typical SPI2: MOSI=11, MISO=13, SCLK=12
constexpr int TFT_MOSI = 23;
constexpr int TFT_MISO = 19;
constexpr int TFT_SCLK = 18;
constexpr int TFT_CS   = 5;
constexpr int TFT_DC   = 2;
constexpr int TFT_RST  = 4;

// ── Analog gauge + indicator pins ─────────────────────────────────────────────
constexpr int RPM_GAUGE_PIN     = 27;
constexpr int TEMP_GAUGE_PIN    = 25;
constexpr int FUEL_GAUGE_PIN    = 26;
constexpr int FUEL_SENSOR_PIN   = 34;  // must be ADC1 (GPIO 32–39); ADC2 unavailable with WiFi
constexpr int WARNING_LIGHT_PIN = 32;
constexpr int FUEL_LIGHT_PIN    = 33;

// ── ECU serial (OpenTurbine ClusterSerial TX → cluster RX) ───────────────────
// TX-only from ECU side — only the cluster RX pin needs wiring.
// OT_CLUSTER_BAUD in hardware_profile.h must match ECU_BAUD here.
#define ECU_SERIAL Serial2
constexpr unsigned long ECU_BAUD   = 460800UL;  // bump from 115200 — 4× faster, fine ≤ 2 m cable
constexpr int           ECU_RX_PIN = 16;
constexpr int           ECU_TX_PIN = 17;        // not connected to ECU (TX-only protocol)
// Serial2 buffer — must cover 3× full OT boot schema burst without overrun
constexpr int ECU_RX_BUF = 1024;

// ── Fuel sender calibration (factory defaults — override via web UI) ──────────
// Reversed sender: high ADC = empty tank
static constexpr int FUEL_RAW_EMPTY = 4050;
static constexpr int FUEL_RAW_FULL  =   79;
static constexpr int FUEL_SAMPLES   =   30;  // rolling average window (seconds)

// ── Data-loss / NO SIGNAL hysteresis (factory defaults) ──────────────────────
static constexpr uint32_t ENTER_LOSS_MS = 2000;
static constexpr uint32_t EXIT_GOOD_MS  =  300;

// ── Engine limits — factory defaults ──────────────────────────────────────────
// These seed ecu.cfg* and the web config on first boot.  The ECU overrides all
// of them at runtime via the L: line in the OT boot schema.
static constexpr float N1_MAX_RPM_DEFAULT   = 100000.0f;
static constexpr float N1_WARN_RPM_DEFAULT  =  90000.0f;
static constexpr float N2_WARN_RPM_DEFAULT  =  22000.0f;
static constexpr float OIL_WARN_BAR_DEFAULT =      2.2f;
static constexpr float TOT_MAX_C_DEFAULT    =    750.0f;
static constexpr float TOT_WARN_C_DEFAULT   =    680.0f;

// Tachometer: square-wave frequency at full scale (tune to match your gauge)
static constexpr float RPM_GAUGE_MAX_HZ = 200.0f;

// ── Fuel warning ──────────────────────────────────────────────────────────────
static constexpr float FUEL_WARN_PCT = 25.0f;

// ── Build-mode defaults (editable via web UI after first boot) ────────────────
// DEMO_MODE_DEFAULT: true = demo sweep, no ECU needed  (useful for bench testing)
// BOOT_SWEEP_DEFAULT: true = sweep RPM+TEMP gauges during boot splash
#define DEMO_MODE_DEFAULT   false
#define BOOT_SWEEP_DEFAULT  true
#define RPM_DISPLAY_PERCENT 0  // 0=×10000rpm  1=%maxN1  (factory default for RuntimeCfg)

// ── Boot splash timing ────────────────────────────────────────────────────────
static constexpr uint32_t BOOT_WAIT_ECU_MS = 5000;  // max wait for OT:1 line
static constexpr uint32_t BOOT_SHOW_ECU_MS = 4500;  // show ECU version for this long

// ── Overlay scroll (overlayActive is always false in OT protocol; retained for compat) ──
static constexpr int      OVERLAY_VISIBLE_CHARS = 40;
static constexpr int      OVERLAY_GAP_CHARS     =  3;
static constexpr uint32_t OVERLAY_SCROLL_MS     = 250;

// ── Temp gauge PWM calibration ────────────────────────────────────────────────
// Maps 0–100% of (valTot / cfgTotMaxC) to PWM duty (0–255).
// Tune with demo mode: set to 100% and adjust until needle reads full scale,
// then save the 100% point. Repeat for other points.
static constexpr float TEMP_GAUGE_PCT[5] = {  0,  25,  50,  75, 100 };
static constexpr int   TEMP_GAUGE_PWM[5] = { 88, 143, 182, 206, 228 };

// ── Fuel gauge PWM calibration ────────────────────────────────────────────────
static constexpr int   FUEL_GAUGE_PCT[5] = {   0,  25,  50,  75, 100 };
static constexpr int   FUEL_GAUGE_PWM[5] = { 110, 138, 159, 184, 217 };

// ── UI layout labels ──────────────────────────────────────────────────────────
static const char* LABELS[3] = { "OIL P", "TOT", "N2" };
static const char* UNITS[3]  = { "bar", "C", "krpm" };
