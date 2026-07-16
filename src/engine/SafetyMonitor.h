#pragma once
#include "EngineData.h"
#include "../system/Config.h"
#include "../system/HardwareConfig.h"
#include <Arduino.h>
#include <functional>
#include <string.h>

// ============================================================
//  SafetyMonitor — reads EngineData, triggers shutdowns on fault
//
//  Overspeed runs every tick (no interval gate) with a short
//  raw-reading confirmation (OVERSPEED_CONFIRM_MS).
//  All other checks run at checkIntervalMs (default 100 ms).
//  All checks bypassable with skipSafetyChecks in DEV_MODE.
//
//  The actual enterShutdown() / setMode() calls happen via
//  callbacks registered at begin() — keeps this file hardware-free.
// ============================================================

class SafetyMonitor {
public:
    using ShutdownFn = void(*)();
    using RelightFn  = std::function<void()>;

    // Config parameters (populated from Config before begin())
    float         rpmLimit              = 100000.0f;
    float         n2RpmLimit            = 0.0f;
    float         minRpm               = 30000.0f;
    float         titLimit             = 0.0f;    // °C — 0 = disabled
    float         oilTempLimit         = 0.0f;    // °C — 0 = disabled
    float         fuelPressMin         = 0.0f;    // bar — 0 = disabled
    float         battVoltMin          = 0.0f;    // V — 0 = disabled
    float         surgeRpmVariance     = 0.0f;    // RPM² variance threshold — 0 = disabled
    float         flameoutShutdownMs   = 3000.0f;
    int           flameoutSource       = 0;
    float         flameoutN1MinRpm     = 0.0f;
    float         flameoutTotDropC     = 80.0f;
    unsigned long checkIntervalMs      = 100;
    float         totRiseRateLimit     = 0.0f;   // °C/s — 0 = disabled

    void begin(ShutdownFn enterShutdown, ShutdownFn enterFault) {
        _enterShutdown    = enterShutdown;
        _enterFault       = enterFault;
        _lastCheckMs      = 0;
        _flameoutMs       = 0;
        _relightStartMs   = 0;
        _relightStartEgt  = 0.0f;
        _runningEgtRef    = -1.0f;
        _refDemand        = 0.0f;
        _startupSpooled   = false;
        _overspeedPending = false;
        _n2OverspeedPending = false;
        _resetEgtRing();
        _refWindowOpen    = false;
    }

    // Allow external callers (e.g. DI fault handler) to inject a fault code
    // so lastFault() returns the right string when enterFaultShutdown() reads it.
    void setExternalFault(const char* code) { _lastFault = code; }

    void setRelightCallback(RelightFn fn) { _relight = fn; }

    void check() {
        auto& ed = EngineData::instance();

        // Mode bookkeeping runs BEFORE the skip/bench gate: mode transitions
        // must reset detection state even while checks are skipped, otherwise
        // a stale _flameoutMs/_runningEgtRef from a previous run survives into
        // the next one and the first un-skipped tick trips instantly (stale
        // absolute timestamp = zero confirmation time).
        SysMode m = ed.mode;
        bool inOp = (m == SysMode::STARTUP || m == SysMode::RUNNING);
        if (!inOp) {
            _flameoutMs     = 0;
            _relightStartMs = 0;
            _relightStartEgt = 0.0f;
            _runningEgtRef   = -1.0f;
            _refDemand       = 0.0f;
            _resetEgtRing();
            _refWindowOpen   = false;
            _overspeedPending = false;
            _n2OverspeedPending = false;
            _lastEgt        = -1.0f;
            _lastEgtMs      = 0;
            ed.totRiseRate  = 0.0f;
            // Surge buffer is only reset on STANDBY entry — not on every non-op
            // mode change (e.g. SHUTDOWN) — so the buffer isn't wiped mid-spindown.
            if (m == SysMode::STANDBY) {
                _n1BufIdx      = 0;
                _n1BufCount    = 0;
                _startupSpooled = false;  // reset for next startup
            }
            ed.surgeDetected = false;
            return;
        }

        if (ed.skipSafetyChecks || ed.benchMode) {
            // Never carry a partly confirmed raw overspeed across a period in
            // which monitoring was deliberately suspended.
            _overspeedPending = false;
            _n2OverspeedPending = false;
            return;
        }

        // Hot start aborts if the configured engine temperature source is still too high.
        const bool hotStartEgt = Config::primaryEgtHealthy(ed)
                              && Config::primaryEgtC(ed) > Config::hotStartTotThreshold;
        if (HardwareConfig::safetyHotStart && m == SysMode::STARTUP
            && Config::hotStartTotThreshold > 0 && hotStartEgt)
        {
            _trigger("HOT_START");
            return;
        }

        // ── Overspeed — every tick, short raw-reading confirmation ──
        // Deliberately ignores n1Healthy: a genuine fast runaway raises the
        // JUMP health flag on every 100 ms sample (rate > jumpThreshold ×
        // rpmLimit/s), which would suppress this exact protection. Instead
        // the raw reading must stay above the limit for OVERSPEED_CONFIRM_MS
        // (≥2, typically 3 sensor samples at the 100 ms sensor update rate),
        // so a single noise spike still cannot trip it.
        if (HardwareConfig::safetyOverspeed && HardwareConfig::hasN1Rpm &&
            ed.n1Rpm > rpmLimit) {
            unsigned long nowOs = millis();
            if (!_overspeedPending) {
                _overspeedPending = true;
                _overspeedSinceMs = nowOs;
            } else if (nowOs - _overspeedSinceMs >= OVERSPEED_CONFIRM_MS) {
                _trigger("OVERSPEED");
                return;
            }
        } else {
            _overspeedPending = false;
        }

        // N2 is an independently protected power-turbine shaft. As with N1,
        // use confirmed raw readings so the sensor's fast-change health flag
        // cannot mask a real free-power-turbine runaway.
        if (HardwareConfig::safetyN2Overspeed && HardwareConfig::hasN2Rpm &&
            n2RpmLimit > 0.0f && ed.n2Rpm > n2RpmLimit) {
            unsigned long nowOs = millis();
            if (!_n2OverspeedPending) {
                _n2OverspeedPending = true;
                _n2OverspeedSinceMs = nowOs;
            } else if (nowOs - _n2OverspeedSinceMs >= OVERSPEED_CONFIRM_MS) {
                _trigger("N2_OVERSPEED");
                return;
            }
        } else {
            _n2OverspeedPending = false;
        }

        // ── Interval checks ──────────────────────────────────
        unsigned long now = millis();
        if (now - _lastCheckMs < checkIntervalMs) return;
        // Gap guard: a hole in monitoring (skip-safety toggled off mid-run,
        // scheduler stall) makes all EGT history stale — reset it so old
        // snapshots can't fake 2 s of stability, and clear any in-progress
        // flameout timer so a stale absolute timestamp can't trip with zero
        // fresh confirmation time.
        if (_lastCheckMs != 0 && now - _lastCheckMs > CHECK_GAP_RESET_MS) {
            _resetEgtRing();
            _lastEgt         = -1.0f;
            _lastEgtMs       = 0;
            ed.totRiseRate   = 0.0f;
            _flameoutMs      = 0;
            _relightStartMs  = 0;
            _relightStartEgt = 0.0f;
        }
        _lastCheckMs = now;

        // EGT rate-of-rise.
        if (Config::primaryEgtHealthy(ed)) {
            float currentEgt = Config::primaryEgtC(ed);
            if (_lastEgt >= 0.0f && _lastEgtMs > 0) {
                float dtSec = (now - _lastEgtMs) / 1000.0f;
                if (dtSec > 0.0f) {
                    ed.totRiseRate = (currentEgt - _lastEgt) / dtSec;
                }
            }
            _lastEgt   = currentEgt;
            _lastEgtMs = now;

            if (totRiseRateLimit > 0.0f && ed.totRiseRate > totRiseRateLimit) {
                _trigger("TOT_RISE");
                return;
            }
        } else {
            _lastEgt   = -1.0f;
            _lastEgtMs = 0;
            ed.totRiseRate = 0.0f;
        }

        const float primaryLimit = Config::primaryEgtLimitC();
        if (HardwareConfig::safetyOvertemp && primaryLimit > 0.0f &&
            Config::primaryEgtHealthy(ed) && Config::primaryEgtC(ed) > primaryLimit) {
            _trigger("OVERTEMP");
            return;
        }

        if (HardwareConfig::safetyLowOil && HardwareConfig::hasOilPress
            && ed.oilMinBar > 0 && ed.oilHealthy && ed.oilPressure < ed.oilMinBar)
        {
            _trigger("LOW_OIL");
            return;
        }

        // Oil near-zero while sensor is ADC-healthy → catastrophic failure or
        // disconnected fitting.  Distinguished from LOW_OIL (calibrated range)
        // and from sensor-rail fault (oilHealthy=false).
        if (HardwareConfig::safetyOilZero && HardwareConfig::hasOilPress
            && m == SysMode::RUNNING && ed.oilHealthy
            && ed.oilPressure < Config::oilZeroBar)
        {
            _trigger("OIL_ZERO");
            return;
        }

        _updateFlameoutReference(ed);
        if (HardwareConfig::safetyFlameout &&
            m == SysMode::RUNNING && ed.flameMonitorActive && _flameoutSourceUsable()) {
            if (_flameoutLost(ed)) {
                if (_flameoutMs == 0) _flameoutMs = now;

                if ((now - _flameoutMs) > (unsigned long)flameoutShutdownMs) {
                    // Relight path: enabled, armed, N1 still viable
                    bool n1Ok = HardwareConfig::hasN1Rpm && ed.n1Healthy
                             && ed.n1Rpm >= Config::relightMinRpm;
                    bool relightIgnitionOk = false;
                    switch (Config::relightIgnitionTarget) {
                        case 1: relightIgnitionOk = HardwareConfig::hasIgniter2; break;
                        case 2: relightIgnitionOk = HardwareConfig::hasGlowPlug; break;
                        default: relightIgnitionOk = HardwareConfig::hasIgniter; break;
                    }
                    if (Config::relightEnabled && ed.relightArmed && relightIgnitionOk && n1Ok && _relight) {
                        if (_relightStartMs == 0) {
                            // First trigger — start continuous ignition
                            _relight();
                            _relightStartMs = now;
                            _relightStartEgt = Config::primaryEgtC(ed);
                        } else {
                            // Relight window: check N1 still viable and timeout not expired
                            bool stillViable = HardwareConfig::hasN1Rpm && ed.n1Healthy
                                            && ed.n1Rpm >= Config::relightMinRpm;
                            bool timedOut    = Config::relightTimeoutMs > 0
                                           && (now - _relightStartMs) > (unsigned long)Config::relightTimeoutMs;
                            if (!stillViable || timedOut) {
                                _trigger("FLAMEOUT");
                                return;
                            }
                        }
                        return;  // checkRelight() in main.cpp keeps igniterOn true each tick
                    }
                    // Relight not enabled / armed / N1 too low — fault immediately
                    _trigger("FLAMEOUT");
                    return;
                }
            } else {
                _flameoutMs     = 0;
                _relightStartEgt = 0.0f;
                _relightStartMs = 0;  // flame returned — reset relight state
            }
        }

        // ── Oil temperature high ──────────────────────────────
        if (HardwareConfig::safetyOilTempHigh && HardwareConfig::hasOilTemp && oilTempLimit > 0.0f
            && ed.oilTempHealthy && ed.oilTemp > oilTempLimit)
        {
            _trigger("OIL_TEMP_HIGH");
            return;
        }

        // ── Fuel pressure low ────────────────────────────────
        if (HardwareConfig::safetyFuelPressLow && HardwareConfig::hasFuelPress && fuelPressMin > 0.0f
            && m == SysMode::RUNNING && ed.fuelPressHealthy
            && ed.fuelPressure < fuelPressMin)
        {
            _trigger("FUEL_PRESS_LOW");
            return;
        }

        // ── Battery / bus undervoltage ────────────────────────
        if (HardwareConfig::safetyBattLow && battVoltMin > 0.0f
            && HardwareConfig::hasBattVoltage
            && ed.battHealthy && ed.battVoltage > 0.5f  // 0.5 V = connected
            && ed.battVoltage < battVoltMin)
        {
            _trigger("BATT_LOW");
            return;
        }

        // ── Surge detection (N1 oscillation variance) ─────────
        if (HardwareConfig::safetySurge && surgeRpmVariance > 0.0f
            && HardwareConfig::hasN1Rpm
            && m == SysMode::RUNNING && ed.n1Healthy)
        {
            // Push N1 sample into circular buffer
            _n1Buf[_n1BufIdx] = ed.n1Rpm;
            _n1BufIdx = (_n1BufIdx + 1) % SURGE_BUF;
            if (_n1BufCount < SURGE_BUF) _n1BufCount++;

            if (_n1BufCount >= SURGE_BUF) {
                // Compute mean then variance
                float sum = 0.0f;
                for (uint8_t i = 0; i < SURGE_BUF; i++) sum += _n1Buf[i];
                float mean = sum / SURGE_BUF;
                float var  = 0.0f;
                for (uint8_t i = 0; i < SURGE_BUF; i++) {
                    float d = _n1Buf[i] - mean;
                    var += d * d;
                }
                var /= SURGE_BUF;

                ed.surgeDetected = (var > surgeRpmVariance);
                if (ed.surgeDetected) {
                    _trigger("SURGE");
                    return;
                }
            }
        } else {
            ed.surgeDetected = false;
        }

        // ── Underspeed ────────────────────────────────────────
        // RUNNING: fire immediately if N1 drops below minRpm at any time.
        // STARTUP: only fire if N1 previously crossed minRpm and then fell back
        //          (genuine stall during spool-up, not the normal crank-up phase
        //          where the engine must pass through 0→minRpm on its way to idle).
        if (HardwareConfig::hasN1Rpm && m == SysMode::RUNNING) {
            if (ed.n1Healthy && ed.n1Rpm < minRpm) {
                if (HardwareConfig::safetyFlameout && ed.flameMonitorActive
                    && _effectiveFlameoutSource() == 2) {
                    return;
                }
                _trigger("UNDERSPEED");
                return;
            }
            if (!ed.n1Healthy && !ed.limpMode) {
                // Brief ZERO_GLITCH samples are tolerated at the sensor
                // (RpmHealth::isTrustworthy — PCNTRpmSensor holds the last real
                // reading), so this only latches on persistent sensor faults.
                ed.limpMode = true;  // RPM sensor lost → limp
            }
        }
        if (HardwareConfig::hasN1Rpm && m == SysMode::STARTUP && ed.n1Healthy) {
            // Track once N1 reaches minRpm so we know the engine has spooled through
            if (ed.n1Rpm >= minRpm) _startupSpooled = true;
            // Only fault if we already spooled past minRpm and now dropped below it
            if (_startupSpooled && ed.n1Rpm < minRpm) {
                _trigger("UNDERSPEED");
                return;
            }
        }
    }

    const char* lastFault() const { return _lastFault; }

private:
    static constexpr uint8_t SURGE_BUF = 10; // ~1 s of N1 samples at 100 ms interval
    // ≥2 (typically 3) fresh 100 ms RPM sensor samples must confirm overspeed.
    static constexpr unsigned long OVERSPEED_CONFIRM_MS = 250;
    // Flameout EGT-reference follow-down (source 3): the reference may only
    // follow EGT to a lower operating point when commanded power has dropped
    // by DEMAND_DROP CUMULATIVELY since the reference was last baselined
    // (_refDemand) — the physical discriminator between a throttle-back
    // (EGT settles lower, demand dropped) and a flameout at constant power
    // (EGT decays, demand unchanged). Cumulative-since-baseline catches
    // arbitrarily slow ramps (a windowed rate test cannot); DEMAND_DROP
    // stays above governor dither.
    static constexpr float         DEMAND_DROP      = 0.10f; // fraction of full-scale demand
    // EGT stability is judged over honest wall time — |ΔEGT| across a ~2 s
    // snapshot ring — never per-tick rates: the default MAX6675 updates every
    // 250 ms while checks run at 100 ms, so consecutive ticks can read the
    // identical value and any rate test would call moving EGT "settled".
    // 2.0 °C over ~2 s ≈ 1 °C/s, and is comfortably above the 0.25 °C
    // quantization of the thermocouple converters.
    static constexpr uint8_t       EGT_RING_SLOTS     = 4;
    static constexpr unsigned long EGT_RING_SAMPLE_MS = 500;  // 4 × 500 ms ≈ 2 s baseline
    static constexpr float         EGT_STABLE_BAND    = 2.0f; // °C over the ring span
    // A hole in monitoring longer than this (skip-safety toggle, stall)
    // invalidates EGT history and any in-progress detection timestamps.
    static constexpr unsigned long CHECK_GAP_RESET_MS = 1500;

    ShutdownFn    _enterShutdown  = nullptr;
    ShutdownFn    _enterFault     = nullptr;
    RelightFn     _relight;
    unsigned long _lastCheckMs    = 0;
    unsigned long _flameoutMs     = 0;
    unsigned long _relightStartMs = 0;   // millis() when relight was first triggered; 0 = not active
    float         _relightStartEgt = 0.0f;
    const char*   _lastFault      = nullptr;
    float         _lastEgt        = -1.0f;   // for dEGT/dt calculation
    unsigned long _lastEgtMs      = 0;
    float         _runningEgtRef  = -1.0f;
    float         _refDemand      = 0.0f;    // throttleDemand captured when the ref was (re)baselined
    bool          _refWindowOpen  = false;   // follow-down window latched open until EGT re-stabilizes
    float         _egtRing[EGT_RING_SLOTS] = {}; // rolling ~2 s EGT snapshots for the stability test
    uint8_t       _egtRingIdx     = 0;
    uint8_t       _egtRingCount   = 0;
    unsigned long _egtRingLastMs  = 0;
    bool          _overspeedPending = false; // raw reading above rpmLimit, confirming
    unsigned long _overspeedSinceMs = 0;     // millis() when the overspeed reading began
    bool          _n2OverspeedPending = false;
    unsigned long _n2OverspeedSinceMs = 0;
    bool          _startupSpooled = false;   // true once N1 ≥ minRpm during STARTUP
    float         _n1Buf[SURGE_BUF] = {};   // circular buffer for surge detection
    uint8_t       _n1BufIdx       = 0;
    uint8_t       _n1BufCount     = 0;

    void _resetEgtRing() {
        _egtRingIdx    = 0;
        _egtRingCount  = 0;
        _egtRingLastMs = 0;
    }

    int _effectiveFlameoutSource() const {
        if (flameoutSource >= 1 && flameoutSource <= 3) return flameoutSource;
        if (HardwareConfig::hasFlame) return 1;
        if (HardwareConfig::hasN1Rpm) return 2;
        if (Config::effectiveEgtSource() != 0) return 3;
        return 0;
    }

    bool _flameoutSourceUsable() const {
        switch (_effectiveFlameoutSource()) {
            case 1: return HardwareConfig::hasFlame;
            case 2: return HardwareConfig::hasN1Rpm;
            case 3: return Config::effectiveEgtSource() != 0;
            default: return false;
        }
    }

    void _updateFlameoutReference(const EngineData& ed) {
        if (_effectiveFlameoutSource() != 3) {
            _runningEgtRef = -1.0f;
            return;
        }
        // Reference is only meaningful in RUNNING — the startup EGT spike
        // would otherwise poison it and read normal post-start EGT as a drop.
        // Resetting the ring here also means it starts filling at RUNNING
        // entry, so the first stability verdict is ≥ 2 s into the run.
        if (ed.mode != SysMode::RUNNING) {
            _runningEgtRef = -1.0f;
            _refDemand     = 0.0f;
            _refWindowOpen = false;
            _resetEgtRing();
            return;
        }

        if (!Config::primaryEgtHealthy(ed)) {
            // Snapshots from before a sensor dropout must not fake stability
            // once it recovers.
            _resetEgtRing();
            return;
        }
        float egt = Config::primaryEgtC(ed);

        // Rolling ~2 s snapshot ring; "stable" = the reading moved less than
        // EGT_STABLE_BAND across the whole span. Two-sided by construction,
        // immune to duplicate sensor reads and converter quantization.
        unsigned long now = millis();
        if (_egtRingLastMs == 0 || now - _egtRingLastMs >= EGT_RING_SAMPLE_MS) {
            _egtRing[_egtRingIdx] = egt;
            _egtRingIdx = (uint8_t)((_egtRingIdx + 1) % EGT_RING_SLOTS);
            if (_egtRingCount < EGT_RING_SLOTS) _egtRingCount++;
            _egtRingLastMs = now;
        }
        // Once full, _egtRingIdx points at the oldest slot (~2 s ago).
        const bool stable = (_egtRingCount >= EGT_RING_SLOTS)
                         && fabsf(egt - _egtRing[_egtRingIdx]) < EGT_STABLE_BAND;

        // First seed after RUNNING entry: only from truly settled EGT. The
        // handoff can arrive mid-spike — on the rising side as easily as the
        // decaying tail (Spool completes on N1, not EGT) — and seeding or
        // ratcheting on a transient peak would read the settle to steady
        // idle EGT as a flameout drop. Until seeded, source 3 is
        // deliberately blind (_flameoutLost needs _runningEgtRef >= 0);
        // UNDERSPEED / a flame sensor cover that window when fitted.
        if (_runningEgtRef < 0.0f) {
            if (stable) {
                _runningEgtRef = egt;
                _refDemand     = ed.throttleDemand;
            }
            return;
        }

        // Follow-down window: opens when commanded power has dropped
        // DEMAND_DROP below the demand captured at the last baseline —
        // CUMULATIVE since baseline, so an arbitrarily slow ramp qualifies
        // once it has travelled 0.10 in total. This is the physical
        // discriminator: a throttle-back lowers EGT with lowered demand, a
        // flameout lowers EGT with demand unchanged. The window latches open
        // until EGT is stable again (a lagged probe settles long after the
        // demand change, and demand wiggle after a chop must not strand a
        // high reference against a still-falling probe); it also un-freezes
        // an in-progress detection — if the operator really pulled power,
        // the drop is not a flameout. KNOWN, DOCUMENTED LIMITATION: a
        // flameout during/shortly after an acknowledged throttle reduction
        // is masked while the window is open (long for heavily lagged
        // probes). With N1 fitted, UNDERSPEED is the backstop; in an
        // EGT-only build nothing detects that case — config validation
        // emits a warning saying exactly that.
        if (!_refWindowOpen && ed.throttleDemand < _refDemand - DEMAND_DROP) {
            _refWindowOpen = true;
        }
        if (_refWindowOpen) {
            if (egt < _runningEgtRef) _runningEgtRef = egt;
            if (stable) {
                // Re-baseline at the new operating point: close the window
                // and re-arm detection.
                _runningEgtRef = egt;
                _refDemand     = ed.throttleDemand;
                _refWindowOpen = false;
            }
            return;
        }

        // Ratchet up — but only to a SETTLED higher operating point. An
        // acceleration overshoot peak is transient (never stable) and must
        // not be captured: against a peak-ratcheted reference, the settle
        // back to steady EGT at held demand would read as a flameout drop.
        // While EGT is above the reference, _flameoutLost is false anyway.
        if (egt > _runningEgtRef) {
            if (stable) {
                _runningEgtRef = egt;
                _refDemand     = ed.throttleDemand;
            }
            return;
        }

        // Constant commanded power, EGT at or below the reference: stay
        // frozen so a genuine flameout accumulates the full drop regardless
        // of probe lag (any real flameout falls far faster than the
        // stability band of ~1 °C/s until the drop is essentially
        // complete). Slow thermal/ambient drift is absorbed only while no
        // detection is in progress — a constant-power detection can never
        // be re-baselined away.
        if (stable && _flameoutMs == 0) {
            _runningEgtRef = egt;
            _refDemand     = ed.throttleDemand;
        }
    }

    bool _flameoutLost(const EngineData& ed) const {
        switch (_effectiveFlameoutSource()) {
            case 1:
                return HardwareConfig::hasFlame && !ed.flameDetected;
            case 2: {
                float threshold = flameoutN1MinRpm > 0.0f ? flameoutN1MinRpm : minRpm;
                return HardwareConfig::hasN1Rpm && ed.n1Healthy && ed.n1Rpm < threshold;
            }
            case 3:
                return Config::primaryEgtHealthy(ed) && _runningEgtRef >= 0.0f
                    && flameoutTotDropC > 0.0f
                    && Config::primaryEgtC(ed) <= (_runningEgtRef - flameoutTotDropC);
            default:
                return false;
        }
    }

    void _trigger(const char* code) {
        _lastFault = code;
        auto& ed = EngineData::instance();

        // Populate plain-language description for the web UI fault banner
        const char* desc = nullptr;
        if      (strcmp(code, "OVERSPEED")  == 0) desc =
            "Engine over-speed: RPM exceeded the safety limit.\n"
            "What to do: Wait for the engine to cool down fully. Check your RPM limit setting "
            "in Config and verify throttle calibration before the next start.";
        else if (strcmp(code, "N2_OVERSPEED") == 0) desc =
            "N2 over-speed: power-turbine RPM exceeded its hard shutdown limit.\n"
            "What to do: Do not restart until the driven load, shaft, coupling, N2 pickup, "
            "governor or propeller control, and configured N2 limit have been inspected.";
        else if (strcmp(code, "OVERTEMP")   == 0) desc =
            "Over-temperature: selected engine temperature source (TOT/TIT) exceeded the limit.\n"
            "What to do: Allow the engine to cool. Check your fuel flow, throttle calibration, "
            "and configured EGT limit. Inspect the turbine for damage if this was severe.";
        else if (strcmp(code, "LOW_OIL")    == 0) desc =
            "Low oil pressure during operation.\n"
            "What to do: Do not restart until you have checked the oil level, oil pump, "
            "oil lines, and fittings for leaks. Verify oil pressure sensor calibration.";
        else if (strcmp(code, "OIL_ZERO")   == 0) desc =
            "Oil pressure read near zero - possible pump failure or broken fitting.\n"
            "What to do: Inspect oil pump, lines, and fittings before any restart. "
            "Do not run the engine until oil supply is confirmed.";
        else if (strcmp(code, "FLAMEOUT")   == 0) desc =
            "Flameout: combustion was lost according to the configured flameout source, and relight was not possible.\n"
            "What to do: Check fuel supply, fuel valve, and the selected flameout sensor/source. "
            "Ensure ignition system is working. Try a normal start.";
        else if (strcmp(code, "UNDERSPEED") == 0) desc =
            "Under-speed: RPM dropped below the minimum running threshold.\n"
            "What to do: Check fuel supply and throttle settings. "
            "Verify the RPM sensor is reading correctly.";
        else if (strcmp(code, "HOT_START")  == 0) desc =
            "Hot start aborted: exhaust temperature was still too high to start safely.\n"
            "What to do: Wait for the engine to cool further before attempting another start. "
            "Increase the cool-down time if this keeps happening.";
        else if (strcmp(code, "TOT_RISE")      == 0) desc =
            "EGT rate-of-rise too fast: temperature is climbing dangerously quickly.\n"
            "What to do: Check for fuel enrichment issues, throttle calibration, "
            "and confirm the EGT rise-rate limit in Config is appropriate for your engine.";
        else if (strcmp(code, "TIT_OVERTEMP")  == 0) desc =
            "Turbine inlet temperature (TIT) exceeded the safety limit.\n"
            "What to do: Allow full cool-down. Check combustion system, fuel flow, and "
            "verify TIT limit is correct for your engine. Inspect turbine wheel for damage.";
        else if (strcmp(code, "OIL_TEMP_HIGH") == 0) desc =
            "Engine oil temperature too high.\n"
            "What to do: Allow the engine to cool down. Check oil cooler (if fitted), "
            "oil level, and flow rate. Reduce run duration until the cause is found.";
        else if (strcmp(code, "FUEL_PRESS_LOW")== 0) desc =
            "Fuel pressure dropped below the minimum threshold during operation.\n"
            "What to do: Check fuel tank level, fuel filter, pump, and lines. "
            "Inspect for leaks or blockages before attempting another run.";
        else if (strcmp(code, "BATT_LOW")      == 0) desc =
            "Battery / bus voltage too low - risk of control system brownout.\n"
            "What to do: Charge or replace the battery. Check power wiring for resistance. "
            "Do not run the engine until the voltage is stable above the limit.";
        else if (strcmp(code, "SURGE")         == 0) desc =
            "Compressor surge detected: N1 RPM is oscillating abnormally.\n"
            "What to do: The engine has been shut down to prevent compressor damage. "
            "Check throttle slew rate settings, compressor inlet for blockage, "
            "and reduce throttle advance rate to prevent recurrence.";
        // Fallback for unknown / DI-channel fault codes — generate a generic message
        char _fallbackDesc[192];
        if (!desc) {
            snprintf(_fallbackDesc, sizeof(_fallbackDesc),
                "Safety fault: %s. Engine has been shut down as a precaution.\n"
                "Check the event log for sensor readings at the time of the fault "
                "and review relevant calibration and limit settings before restarting.",
                code);
            desc = _fallbackDesc;
        }

        ed.faultDescription[0] = '\0';  // clear previous fault message before writing
        strncpy(ed.faultDescription, desc, sizeof(ed.faultDescription) - 1);
        ed.faultDescription[sizeof(ed.faultDescription) - 1] = '\0';

        if (_enterFault) _enterFault();   // enterFaultShutdown() — logs FAULT:*, sets lastEvent
    }
};
