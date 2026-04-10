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
//  Overspeed is always immediate (no interval gate).
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
    float         minRpm               = 30000.0f;
    float         totLimit             = 750.0f;
    float         titLimit             = 0.0f;    // °C — 0 = disabled
    float         oilTempLimit         = 0.0f;    // °C — 0 = disabled
    float         fuelPressMin         = 0.0f;    // bar — 0 = disabled
    float         battVoltMin          = 0.0f;    // V — 0 = disabled
    float         surgeRpmVariance     = 0.0f;    // RPM² variance threshold — 0 = disabled
    float         flameoutShutdownMs   = 3000.0f;
    unsigned long checkIntervalMs      = 100;
    float         totRiseRateLimit     = 0.0f;   // °C/s — 0 = disabled

    void begin(ShutdownFn enterShutdown, ShutdownFn enterFault) {
        _enterShutdown = enterShutdown;
        _enterFault    = enterFault;
        _lastCheckMs   = 0;
        _flameoutMs    = 0;
    }

    void setRelightCallback(RelightFn fn) { _relight = fn; }

    // Reset the flameout timer so SafetyMonitor waits a fresh flameoutShutdownMs
    // before firing another relight attempt. Call when a per-attempt igniter times out.
    void resetFlameoutTimer() { _flameoutMs = 0; }

    void check() {
        auto& ed = EngineData::instance();
        if (ed.skipSafetyChecks || ed.benchMode) return;

        SysMode m = ed.mode;
        bool inOp = (m == SysMode::STARTUP || m == SysMode::RUNNING);
        if (!inOp) {
            _flameoutMs    = 0;
            _lastTot       = -1.0f;
            _lastTotMs     = 0;
            ed.totRiseRate = 0.0f;
            // Surge buffer is only reset on STANDBY entry — not on every non-op
            // mode change (e.g. SHUTDOWN) — so the buffer isn't wiped mid-spindown.
            if (m == SysMode::STANDBY) {
                _n1BufIdx   = 0;
                _n1BufCount = 0;
            }
            ed.surgeDetected = false;
            return;
        }

        // ── Hot start — abort startup if TOT still too high ──
        if (HardwareConfig::safetyHotStart && m == SysMode::STARTUP
            && ed.totHealthy && Config::hotStartTotThreshold > 0
            && ed.tot > Config::hotStartTotThreshold)
        {
            _trigger("HOT_START");
            return;
        }

        // ── Overspeed — always immediate ─────────────────────
        if (HardwareConfig::safetyOverspeed && ed.n1Healthy && ed.n1Rpm > rpmLimit) {
            _trigger("OVERSPEED");
            return;
        }

        // ── Interval checks ──────────────────────────────────
        unsigned long now = millis();
        if (now - _lastCheckMs < checkIntervalMs) return;
        _lastCheckMs = now;

        // ── EGT rate-of-rise ─────────────────────────────────
        if (ed.totHealthy) {
            float currentTot = ed.tot;
            if (_lastTot >= 0.0f && _lastTotMs > 0) {
                float dtSec = (now - _lastTotMs) / 1000.0f;
                if (dtSec > 0.0f) {
                    ed.totRiseRate = (currentTot - _lastTot) / dtSec;
                }
            }
            _lastTot   = currentTot;
            _lastTotMs = now;

            if (totRiseRateLimit > 0.0f && ed.totRiseRate > totRiseRateLimit) {
                _trigger("TOT_RISE");
                return;
            }
        } else {
            _lastTot   = -1.0f;
            _lastTotMs = 0;
            ed.totRiseRate = 0.0f;
        }

        if (HardwareConfig::safetyOvertemp && ed.totHealthy && ed.tot > totLimit) {
            _trigger("OVERTEMP");
            return;
        }

        if (HardwareConfig::safetyLowOil
            && ed.oilMinBar > 0 && ed.oilHealthy && ed.oilPressure < ed.oilMinBar)
        {
            _trigger("LOW_OIL");
            return;
        }

        // Oil near-zero while sensor is ADC-healthy → catastrophic failure or
        // disconnected fitting.  Distinguished from LOW_OIL (calibrated range)
        // and from sensor-rail fault (oilHealthy=false).
        if (HardwareConfig::safetyOilZero
            && m == SysMode::RUNNING && ed.oilHealthy
            && ed.oilPressure < Config::oilZeroBar)
        {
            _trigger("OIL_ZERO");
            return;
        }

        if (HardwareConfig::safetyFlameout && m == SysMode::RUNNING && ed.flameMonitorActive) {
            if (!ed.flameDetected) {
                if (_flameoutMs == 0) _flameoutMs = now;
                if ((now - _flameoutMs) > (unsigned long)flameoutShutdownMs) {
                    // Attempt relight before faulting, if conditions allow
                    // Keeps retrying until N1 falls below relightMinRpm
                    // Relight: attempt only when enabled, armed, RPM still viable,
                    // and not yet exceeded the max-attempt cap (0 = unlimited).
                    bool underLimit = (Config::relightMaxAttempts == 0)
                                   || (ed.relightAttempts < (uint8_t)Config::relightMaxAttempts);
                    if (Config::relightEnabled
                        && ed.relightArmed
                        && ed.n1Rpm >= Config::relightMinRpm
                        && underLimit
                        && _relight)
                    {
                        _relight();
                        _flameoutMs = now;  // reset timer — give relight time to work
                        return;
                    }
                    _trigger("FLAMEOUT");
                    return;
                }
            } else {
                _flameoutMs = 0;
            }
        }

        // ── TIT overtemp ─────────────────────────────────────
        if (HardwareConfig::safetyTitOvertemp && titLimit > 0.0f
            && ed.titHealthy && ed.tit > titLimit)
        {
            _trigger("TIT_OVERTEMP");
            return;
        }

        // ── Oil temperature high ──────────────────────────────
        if (HardwareConfig::safetyOilTempHigh && oilTempLimit > 0.0f
            && ed.oilTempHealthy && ed.oilTemp > oilTempLimit)
        {
            _trigger("OIL_TEMP_HIGH");
            return;
        }

        // ── Fuel pressure low ────────────────────────────────
        if (HardwareConfig::safetyFuelPressLow && fuelPressMin > 0.0f
            && m == SysMode::RUNNING && ed.fuelPressHealthy
            && ed.fuelPressure < fuelPressMin)
        {
            _trigger("FUEL_PRESS_LOW");
            return;
        }

        // ── Battery / bus undervoltage ────────────────────────
        if (HardwareConfig::safetyBattLow && battVoltMin > 0.0f
            && ed.battHealthy && ed.battVoltage > 0.5f  // 0.5 V = connected
            && ed.battVoltage < battVoltMin)
        {
            _trigger("BATT_LOW");
            return;
        }

        // ── Surge detection (N1 oscillation variance) ─────────
        if (HardwareConfig::safetySurge && surgeRpmVariance > 0.0f
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
        // Checked in both RUNNING and STARTUP (after initial spin-up).
        // In STARTUP only trigger if RPM is already above zero — avoids false
        // trips before the starter has begun spinning the engine.
        if (m == SysMode::RUNNING) {
            if (ed.n1Healthy && ed.n1Rpm < minRpm) {
                _trigger("UNDERSPEED");
                return;
            }
            if (!ed.n1Healthy && !ed.limpMode) {
                ed.limpMode = true;  // RPM sensor lost → limp
            }
        }
        if (m == SysMode::STARTUP) {
            if (ed.n1Rpm > 0 && ed.n1Rpm < minRpm && ed.n1Healthy) {
                _trigger("UNDERSPEED");
                return;
            }
        }
    }

    const char* lastFault() const { return _lastFault; }

private:
    static constexpr uint8_t SURGE_BUF = 10; // ~1 s of N1 samples at 100 ms interval

    ShutdownFn    _enterShutdown = nullptr;
    ShutdownFn    _enterFault    = nullptr;
    RelightFn     _relight;
    unsigned long _lastCheckMs   = 0;
    unsigned long _flameoutMs    = 0;
    const char*   _lastFault     = nullptr;
    float         _lastTot       = -1.0f;   // for dEGT/dt calculation
    unsigned long _lastTotMs     = 0;
    float         _n1Buf[SURGE_BUF] = {};   // circular buffer for surge detection
    uint8_t       _n1BufIdx      = 0;
    uint8_t       _n1BufCount    = 0;

    void _trigger(const char* code) {
        _lastFault = code;
        auto& ed = EngineData::instance();

        // Populate plain-language description for the web UI fault banner
        const char* desc = nullptr;
        if      (strcmp(code, "OVERSPEED")  == 0) desc =
            "Engine over-speed: RPM exceeded the safety limit.\n"
            "What to do: Wait for the engine to cool down fully. Check your RPM limit setting "
            "in Config and verify throttle calibration before the next start.";
        else if (strcmp(code, "OVERTEMP")   == 0) desc =
            "Over-temperature: Exhaust gas temperature (TOT/EGT) exceeded the limit.\n"
            "What to do: Allow the engine to cool. Check your fuel flow, throttle calibration, "
            "and TOT limit setting. Inspect the turbine for damage if this was severe.";
        else if (strcmp(code, "LOW_OIL")    == 0) desc =
            "Low oil pressure during operation.\n"
            "What to do: Do not restart until you have checked the oil level, oil pump, "
            "oil lines, and fittings for leaks. Verify oil pressure sensor calibration.";
        else if (strcmp(code, "OIL_ZERO")   == 0) desc =
            "Oil pressure read near zero — possible pump failure or broken fitting.\n"
            "What to do: Inspect oil pump, lines, and fittings before any restart. "
            "Do not run the engine until oil supply is confirmed.";
        else if (strcmp(code, "FLAMEOUT")   == 0) desc =
            "Flameout: the flame sensor lost flame and relight was not possible.\n"
            "What to do: Check fuel supply, fuel valve, and flame sensor. "
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
            "and confirm the TOT rise-rate limit in Config is appropriate for your engine.";
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
            "Battery / bus voltage too low — risk of control system brownout.\n"
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
                "Check the flight log for sensor readings at the time of the fault "
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
