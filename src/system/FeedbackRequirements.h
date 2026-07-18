#pragma once

#include "Config.h"
#include "HardwareConfig.h"
#include <string.h>

// Feedback is operationally required only when an enabled protection,
// controller, or configured startup block consumes it. Merely fitting a
// telemetry sensor must never block START, cap fuel, or latch limp mode.
namespace FeedbackRequirements {
    inline bool startupHas(const char* name) {
        for (int i = 0; i < HardwareConfig::startupSeqLen; ++i)
            if (!strcmp(HardwareConfig::startupSeq[i], name)) return true;
        return false;
    }

    inline int effectiveFlameoutSource() {
        if (Config::flameoutSource >= 1 && Config::flameoutSource <= 3)
            return Config::flameoutSource;
        if (HardwareConfig::hasFlame) return 1;
        if (HardwareConfig::hasN1Rpm) return 2;
        if (Config::effectiveEgtSource() != 0) return 3;
        return 0;
    }

    inline bool n1ForProtectionOrControl() {
        return HardwareConfig::safetyOverspeed || HardwareConfig::safetySurge ||
               (HardwareConfig::hasN1Rpm && Config::minRpm > 0.0f) ||
               (HardwareConfig::safetyFlameout && effectiveFlameoutSource() == 2) ||
               (HardwareConfig::hasDynamicIdle && HardwareConfig::hasN1Rpm && !Config::idleUseN2) ||
               (HardwareConfig::hasThrottleSlew && HardwareConfig::hasN1Rpm && Config::pullbackN1Enabled &&
                Config::pullbackN1HardRpm > Config::pullbackN1SoftRpm);
    }

    inline bool n2ForProtectionOrControl() {
        return HardwareConfig::safetyN2Overspeed || HardwareConfig::hasGovernor ||
               (HardwareConfig::hasDynamicIdle && HardwareConfig::hasN2Rpm && Config::idleUseN2) ||
               (HardwareConfig::hasThrottleSlew && HardwareConfig::hasN2Rpm && Config::pullbackN2Enabled &&
                Config::pullbackN2HardRpm > Config::pullbackN2SoftRpm);
    }

    inline bool egtForProtectionOrControl() {
        return HardwareConfig::safetyOvertemp || HardwareConfig::safetyHotStart ||
               Config::totRiseRateLimitDegPerSec > 0.0f ||
               (HardwareConfig::safetyFlameout && effectiveFlameoutSource() == 3) ||
               (HardwareConfig::hasThrottleSlew && Config::effectiveEgtSource() != 0 && Config::pullbackEgtEnabled &&
                Config::pullbackEgtHardC > Config::pullbackEgtSoftC);
    }

    inline bool n1ForStart() {
        return n1ForProtectionOrControl() || startupHas("StarterSpin") ||
               startupHas("Spool") || startupHas("SafetyHold");
    }
    inline bool n2ForStart() {
        return n2ForProtectionOrControl() || startupHas("GovernorHold");
    }
    inline bool egtForStart() {
        return egtForProtectionOrControl() || startupHas("TempConfirm") ||
               startupHas("WaitTOTCool");
    }

    inline bool oilPressureForStart() {
        if (!HardwareConfig::hasOilPress) return false;
        return HardwareConfig::safetyLowOil || HardwareConfig::safetyOilZero ||
               HardwareConfig::hasOilLoop || startupHas("OilPrime") ||
               startupHas("StarterSpin") || startupHas("Spool") ||
               startupHas("SafetyHold");
    }

    inline bool flameForStart() {
        return HardwareConfig::hasFlame && HardwareConfig::safetyFlameout &&
               effectiveFlameoutSource() == 1;
    }

    // Telemetry-only sensors are intentionally absent. Every member of this
    // set is consumed by an enabled safety, controller, or startup block.
    inline bool allRequiredStartFeedbackHealthy(const EngineData& ed, uint32_t now) {
        if (n1ForStart() && (!HardwareConfig::hasN1Rpm || !ed.n1Healthy || now - ed.n1SampleMs > 500UL))
            return false;
        if (n2ForStart() && (!HardwareConfig::hasN2Rpm || !ed.n2Healthy || now - ed.n2SampleMs > 500UL))
            return false;
        if (egtForStart()) {
            const uint32_t sampleMs = Config::effectiveEgtSource() == 2 ? ed.titSampleMs : ed.totSampleMs;
            if (Config::effectiveEgtSource() == 0 || !Config::primaryEgtHealthy(ed) ||
                now - sampleMs > 1000UL) return false;
        }
        if (oilPressureForStart() && !ed.oilHealthy) return false;
        if (HardwareConfig::safetyOilTempHigh && HardwareConfig::hasOilTemp && !ed.oilTempHealthy) return false;
        if (HardwareConfig::safetyFuelPressLow && HardwareConfig::hasFuelPress && !ed.fuelPressHealthy) return false;
        if (HardwareConfig::safetyBattLow && HardwareConfig::hasBattVoltage && !ed.battHealthy) return false;
        if (flameForStart() && !ed.flameHealthy) return false;
        if (HardwareConfig::hasThrottleInput && !ed.throttleInputValid) return false;
        if (HardwareConfig::hasIdleInput &&
            (startupHas("FuelPumpIdle") || startupHas("ModifiedIdle")) && !ed.idleInputValid) return false;
        if (Config::glowWaitUntilHot && startupHas("GlowPreheat") &&
            (!HardwareConfig::hasGlowCurrentSensor || !ed.glowCurrentHealthy)) return false;
        return true;
    }
}
