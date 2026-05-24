#include "system/version.h"
#include "Hardware.h"
#include "platform/esp32/PlatformInit.h"
#include "system/Config.h"
#include "system/HardwareConfig.h"
#include "system/ClusterSerial.h"
#include "system/FlightRecorder.h"
#include "system/SessionLogger.h"
#include "system/CommandQueue.h"
#include "system/Watchdog.h"
#include "system/web/WebServer.h"
#include "system/MAVLinkOutput.h"
#include "system/RulesEngine.h"
#include "engine/EngineData.h"
#include "hal/RCInput.h"

// ── MAVLink serial output ─────────────────────────────────────
static MAVLinkOutput g_mavlink;
static HardwareSerial _mavSerial(2);  // UART2

// ── Global hardware objects (always compiled in) ──────────────
OT_DECLARE_HARDWARE;

// ── Sequence arrays — built at runtime from hardware.json ─────
// Block name → pointer registry (all sequence blocks)
struct BlockEntry { const char* name; IBlock* blk; };
static const BlockEntry _blockRegistry[] = {
    // Core sequence blocks
    {"OilPrime",      &g_blkOilPrime},
    {"StarterSpin",   &g_blkStarterSpin},
    {"PreIgnSpark",   &g_blkPreIgnSpark},
    {"FuelOpen",      &g_blkFuelOpen},
    {"FlameConfirm",  &g_blkFlameConfirm},
    {"TempConfirm",   &g_blkTempConfirm},
    {"TimedDelay",    &g_blkTimedDelay},
    {"FuelPumpIdle",  &g_blkFuelPumpIdle},
    {"ModifiedIdle",  &g_blkModifiedIdle},
    {"Spool",         &g_blkSpool},
    {"SafetyHold",    &g_blkSafetyHold},
    {"ImmediateCut",  &g_blkImmediateCut},
    {"RPMDrop",       &g_blkRPMDrop},
    {"CooldownSpin",  &g_blkCooldownSpin},
    {"FinalStop",     &g_blkFinalStop},
    // Simple actuator blocks
    {"IgniterOn",     &g_blkIgniterOn},
    {"IgniterOff",    &g_blkIgniterOff},
    {"FuelSolClose",  &g_blkFuelSolClose},
    {"StarterEnOn",   &g_blkStarterEnOn},
    {"StarterEnOff",  &g_blkStarterEnOff},
    {"StarterOff",    &g_blkStarterOff},
    {"OilPumpOn",     &g_blkOilPumpOn},
    {"OilPumpOff",    &g_blkOilPumpOff},
    {"CoolFanOn",     &g_blkCoolFanOn},
    {"CoolFanOff",    &g_blkCoolFanOff},
    {"AirstarterOn",  &g_blkAirstarterOn},
    {"AirstarterOff", &g_blkAirstarterOff},
    {"ABPumpOn",      &g_blkABPumpOn},
    {"ABPumpOff",     &g_blkABPumpOff},
    {"ABIgnOn",       &g_blkABIgnOn},
    {"ABIgnOff",      &g_blkABIgnOff},
    {"OilScavengeOn",  &g_blkOilScavengeOn},
    {"OilScavengeOff", &g_blkOilScavengeOff},
    // Extended blocks
    {"FuelPulse",      &g_blkFuelPulse},
    {"WaitTOTCool",    &g_blkWaitTOTCool},
    {"WaitForInput",   &g_blkWaitForInput},
    {"ThrottleSet",    &g_blkThrottleSet},
    {"PreHeat",        &g_blkPreHeat},
    // Advanced / extended hardware blocks
    {"BleedOpen",      &g_blkBleedOpen},
    {"BleedClose",     &g_blkBleedClose},
    {"GlowPreheat",    &g_blkGlowPreheat},
    {"FuelPumpRamp",   &g_blkFuelPumpRamp},
    {"FuelPump2Set",   &g_blkFuelPump2Set},
    {"GovernorHold",   &g_blkGovernorHold},
    // Afterburner blocks
    {"ABSolOpen",      &g_blkABSolOpen},
    {"ABSolClose",     &g_blkABSolClose},
    {"ABCheckReady",   &g_blkABCheckReady},
    {"ABIgnite",       &g_blkABIgnite},
    {"ABFlameConfirm", &g_blkABFlameConfirm},
    {"ABStabilize",    &g_blkABStabilize},
};
static constexpr size_t _blockRegistryLen = sizeof(_blockRegistry) / sizeof(BlockEntry);

static IBlock* _startupBlocks[HardwareConfig::MAX_SEQ_BLOCKS];
static int     _startupCount  = 0;
static IBlock* _shutdownBlocks[HardwareConfig::MAX_SEQ_BLOCKS];
static int     _shutdownCount = 0;
static IBlock* _abIgnBlocks[HardwareConfig::MAX_SEQ_BLOCKS];
static int     _abIgnCount    = 0;
static IBlock* _abShutBlocks[HardwareConfig::MAX_SEQ_BLOCKS];
static int     _abShutCount   = 0;
static void validateSequences();  // defined after buildSequences

static void buildSequences() {
    auto& hw = HardwareConfig::instance();
    _startupCount = 0;
    for (int i = 0; i < hw.startupSeqLen; i++) {
        for (size_t j = 0; j < _blockRegistryLen; j++) {
            if (strcmp(_blockRegistry[j].name, hw.startupSeq[i]) == 0) {
                _startupBlocks[_startupCount++] = _blockRegistry[j].blk;
                break;
            }
        }
    }
    _shutdownCount = 0;
    for (int i = 0; i < hw.shutdownSeqLen; i++) {
        for (size_t j = 0; j < _blockRegistryLen; j++) {
            if (strcmp(_blockRegistry[j].name, hw.shutdownSeq[i]) == 0) {
                _shutdownBlocks[_shutdownCount++] = _blockRegistry[j].blk;
                break;
            }
        }
    }
    // AB ignition sequence
    _abIgnCount = 0;
    for (int i = 0; i < hw.abSeqLen; i++) {
        for (size_t j = 0; j < _blockRegistryLen; j++) {
            if (strcmp(_blockRegistry[j].name, hw.abSeq[i]) == 0) {
                _abIgnBlocks[_abIgnCount++] = _blockRegistry[j].blk;
                break;
            }
        }
    }
    // AB shutdown sequence
    _abShutCount = 0;
    for (int i = 0; i < hw.abShutSeqLen; i++) {
        for (size_t j = 0; j < _blockRegistryLen; j++) {
            if (strcmp(_blockRegistry[j].name, hw.abShutSeq[i]) == 0) {
                _abShutBlocks[_abShutCount++] = _blockRegistry[j].blk;
                break;
            }
        }
    }
    Serial.printf("[OT] Sequences: startup=%d, shutdown=%d, ab_ign=%d, ab_shut=%d blocks\n",
                  _startupCount, _shutdownCount, _abIgnCount, _abShutCount);
    validateSequences();
}

// ── Sequence hardware validation ──────────────────────────────
// Checks each block in the active sequences against the configured
// hardware. Issues are stored in EngineData for telemetry / UI display.
// Errors (isError=true) block the START command unless bench mode is on.
// Warnings (isError=false) are informational — the sequence will proceed
// but may time out or behave differently than expected.
static void validateSequences() {
    auto& hw = HardwareConfig::instance();
    auto& ed = EngineData::instance();
    ed.seqIssueCount = 0;
    ed.seqHasErrors  = false;

    auto addIssue = [&](const char* block, const char* reason, bool isError) {
        if (ed.seqIssueCount >= EngineData::MAX_SEQ_ISSUES) return;
        auto& iss = ed.seqIssues[ed.seqIssueCount++];
        strncpy(iss.blockName, block, sizeof(iss.blockName) - 1);
        iss.blockName[sizeof(iss.blockName) - 1] = '\0';
        strncpy(iss.reason, reason, sizeof(iss.reason) - 1);
        iss.reason[sizeof(iss.reason) - 1] = '\0';
        iss.isError = isError;
        if (isError) ed.seqHasErrors = true;
        Serial.printf("[VALIDATE] %s %s: %s\n",
                      isError ? "ERROR" : "WARN", block, reason);
    };

    // ── Check for unrecognized block names ────────────────────
    // Unknown names are silently skipped by buildSequences() — flag them so
    // the user can see exactly which name is wrong rather than just having
    // a mysteriously shorter sequence.
    auto checkNames = [&](const char* seqLabel,
                          const char (*names)[24],
                          int len) {
        for (int i = 0; i < len; i++) {
            if (!names[i][0]) continue;
            bool found = false;
            for (size_t j = 0; j < _blockRegistryLen; j++) {
                if (strcmp(_blockRegistry[j].name, names[i]) == 0) { found = true; break; }
            }
            if (!found) {
                char reason[80];
                snprintf(reason, sizeof(reason),
                         "Unknown block in %s sequence — will be skipped", seqLabel);
                // Use block name as the key (truncated to fit blockName field)
                char truncated[24];
                strncpy(truncated, names[i], sizeof(truncated) - 1);
                truncated[sizeof(truncated) - 1] = '\0';
                addIssue(truncated, reason, true);
            }
        }
    };
    checkNames("startup",  hw.startupSeq,  hw.startupSeqLen);
    checkNames("shutdown", hw.shutdownSeq, hw.shutdownSeqLen);
    checkNames("ab_ign",   hw.abSeq,       hw.abSeqLen);
    checkNames("ab_shut",  hw.abShutSeq,   hw.abShutSeqLen);

    // ── Check startup blocks ──────────────────────────────────
    for (int i = 0; i < _startupCount; i++) {
        const char* nm = _startupBlocks[i]->name();

        if (strcmp(nm, "StarterSpin") == 0) {
            if (!hw.hasN1Rpm)
                addIssue(nm, "Needs N1 RPM sensor — will hang for full timeout then fault", true);
            if (!hw.hasStarter)
                addIssue(nm, "No starter actuator configured — block will run with no physical effect", false);
        }
        else if (strcmp(nm, "Spool") == 0) {
            if (!hw.hasN1Rpm)
                addIssue(nm, "Needs N1 RPM sensor — will hang for full timeout then fault", true);
        }
        else if (strcmp(nm, "SafetyHold") == 0) {
            if (!hw.hasN1Rpm && !hw.hasOilPress)
                addIssue(nm, "No N1 RPM or oil pressure sensor — health check will fault before reaching RUNNING", true);
        }
        else if (strcmp(nm, "FlameConfirm") == 0) {
            if (!hw.hasFlame)
                // ERROR (not warning): without a flame sensor this block always aborts startup.
                // Bench mode bypasses the error gate so testing still works.
                addIssue(nm, "No flame sensor fitted — FlameConfirm will always abort startup. "
                             "Replace with TempConfirm (TOT sensor) or TimedDelay, "
                             "or enable Bench Mode to test without sensors.", true);
        }
        else if (strcmp(nm, "TempConfirm") == 0) {
            if (!hw.hasTot)
                // ERROR: same logic — TempConfirm without a TOT sensor always aborts.
                addIssue(nm, "No TOT sensor fitted — TempConfirm will always abort startup. "
                             "Replace with FlameConfirm (flame sensor) or TimedDelay, "
                             "or enable Bench Mode to test without sensors.", true);
        }
        else if (strcmp(nm, "OilPrime") == 0) {
            if (!hw.hasOilPump)
                addIssue(nm, "No oil pump actuator configured — block will run for timeout with no physical effect", false);
        }
        else if (strcmp(nm, "GlowPreheat") == 0) {
            if (!hw.hasGlowPlug)
                addIssue(nm, "No glow plug configured — block will complete immediately with no effect", false);
        }
        else if (strcmp(nm, "PreIgnSpark") == 0) {
            if (!hw.hasIgniter)
                addIssue(nm, "No igniter configured — block will complete with no spark", false);
        }
        else if (strcmp(nm, "FuelOpen") == 0) {
            if (!hw.hasFuelSol)
                addIssue(nm, "No fuel solenoid configured — fuel will not open", false);
        }
        else if (strcmp(nm, "BleedOpen") == 0 || strcmp(nm, "BleedClose") == 0) {
            if (!hw.hasBleedValve)
                addIssue(nm, "No bleed valve configured — block will complete with no effect", false);
        }
        else if (strcmp(nm, "FuelPump2Set") == 0 || strcmp(nm, "FuelPumpRamp") == 0) {
            if (!hw.hasFuelPump2)
                addIssue(nm, "No secondary fuel pump configured — block will complete with no effect", false);
        }
        else if (strcmp(nm, "GovernorHold") == 0) {
            if (!hw.hasN2Rpm)
                addIssue(nm, "No N2 RPM sensor — GovernorHold will time out with no feedback", false);
        }
    }

    // ── Startup sequence structural checks ────────────────────
    if (_startupCount == 0) {
        addIssue("startup", "Startup sequence is empty — engine will jump to RUNNING with no checks or actuator commands", true);
    } else {
        // Warn if no block that opens fuel is present.
        // FuelOpen is the canonical path; FuelPulse is an alternative for pulsed fuel.
        bool hasFuelDelivery = false;
        for (int i = 0; i < _startupCount; i++) {
            const char* nm = _startupBlocks[i]->name();
            if (strcmp(nm, "FuelOpen") == 0 || strcmp(nm, "FuelPulse") == 0) {
                hasFuelDelivery = true; break;
            }
        }
        if (!hasFuelDelivery)
            addIssue("FuelOpen", "No fuel delivery block (FuelOpen/FuelPulse) in startup — engine will spin without fuel", false);
    }

    // ── Check shutdown blocks ─────────────────────────────────
    for (int i = 0; i < _shutdownCount; i++) {
        const char* nm = _shutdownBlocks[i]->name();
        if (strcmp(nm, "RPMDrop") == 0 && !hw.hasN1Rpm)
            addIssue(nm, "No N1 RPM sensor — will wait for full timeout then proceed", false);
        else if (strcmp(nm, "CooldownSpin") == 0 && !hw.hasStarter && !hw.hasCoolFan)
            addIssue(nm, "No starter or cooling fan — cooldown will run for timeout with no airflow", false);
    }

    // ── Check AB ignition blocks ──────────────────────────────
    // AB is optional equipment — issues here should never block main engine START.
    // Skip entirely if no AB hardware is fitted (hasAbSol / hasAbPump).
    const bool abFitted = hw.hasAbSol || hw.hasAbPump;
    if (abFitted) {
        for (int i = 0; i < _abIgnCount; i++) {
            const char* nm = _abIgnBlocks[i]->name();
            if (strcmp(nm, "ABIgnite") == 0) {
                if (!hw.hasIgniter && !hw.hasAbSol)
                    addIssue(nm, "No igniter or AB solenoid configured — AB ignition will have no effect", false);
                // Torch is silently skipped at runtime when torchTotLimit == 0.
                // Warn so the user knows to set abTorchTotLimit in config.
                if (g_blkABIgnite.useTorch && g_blkABIgnite.torchTotLimit == 0.0f)
                    addIssue(nm, "useTorch=true but abTorchTotLimit is 0 — torch will be silently disabled at runtime (no TOT safety cap). Set abTorchTotLimit > 0 in engine settings to enable torch.", false);
            }
            else if (strcmp(nm, "ABFlameConfirm") == 0) {
                if (!hw.hasFlame && !hw.hasTot)
                    addIssue(nm, "No flame sensor or TOT sensor — AB flame confirm will abort on timeout", false);
            }
        }
    }

    // ── Config sanity checks (not tied to a specific block) ──────
    // idleUseN2=true without N2 sensor: DynamicIdle reads n2Healthy=true
    // by default (unfitted → no fault), so RPM will read 0 and the controller
    // ramps throttle to its ceiling with no feedback — effectively a runaway.
    if (Config::idleUseN2 && !hw.hasN2Rpm)
        addIssue("DynamicIdle", "idleUseN2=true but no N2 RPM sensor configured — "
                                "DynamicIdle will ramp throttle to maximum with no feedback. "
                                "Disable idleUseN2 or configure an N2 sensor.", true);

    if (ed.seqIssueCount == 0)
        Serial.println("[VALIDATE] All sequences OK");
}

// Forward declarations for helpers that call mode-transition functions
// defined later in this file.
static void enterStandby();
static void enterShutdown();
static void enterFaultShutdown();

// ── General-purpose DI debounce state ────────────────────────
static unsigned long _diLastChange[HardwareConfig::MAX_DI] = {};
static bool          _diRawLast[HardwareConfig::MAX_DI]    = {};

// ── Run-time tracking ─────────────────────────────────────────
static unsigned long _runStartMs            = 0;   // millis() when RUNNING entered

// ── Buzzer state machine ───────────────────────────────────────
// Drives a passive piezo on buzzerPin via tone()/noTone() (Arduino API).
// Patterns:
//   0 = silence
//   1 = fault     — rapid 2500 Hz beep (repeating, 100 ms on/off)
//   2 = RUNNING   — single 1800 Hz 500 ms beep (one-shot)
//   3 = STARTUP   — double chirp 1500 Hz (two 100 ms beeps, 150 ms gap, one-shot)
//   4 = SHUTDOWN  — single low 900 Hz 400 ms beep (one-shot)
static uint8_t       _buzzerPattern         = 0;
static uint8_t       _buzzerStep            = 0;
static unsigned long _buzzerNextMs          = 0;
static bool          _buzzerToneOn          = false;

static void buzzerTick() {
    if (!HardwareConfig::hasBuzzer || HardwareConfig::buzzerPin < 0) return;
    unsigned long now = millis();
    if (now < _buzzerNextMs) return;
    if (_buzzerPattern == 0) {
        if (_buzzerToneOn) { noTone(HardwareConfig::buzzerPin); _buzzerToneOn = false; }
        return;
    }
    if (_buzzerPattern == 1) {  // fault: 100ms on / 100ms off rapid beep
        if (_buzzerToneOn) { noTone(HardwareConfig::buzzerPin); _buzzerToneOn = false; _buzzerNextMs = now + 100; }
        else               { tone(HardwareConfig::buzzerPin, 2500, 100); _buzzerToneOn = true; _buzzerNextMs = now + 100; }
    } else if (_buzzerPattern == 2) {  // RUNNING: 500ms single beep then stop
        tone(HardwareConfig::buzzerPin, 1800, 500);
        _buzzerPattern = 0; _buzzerStep = 0;
        _buzzerNextMs  = now + 500;
    } else if (_buzzerPattern == 3) {  // STARTUP begin: double chirp then stop
        if (_buzzerStep == 0) {
            tone(HardwareConfig::buzzerPin, 1500, 100);
            _buzzerStep = 1; _buzzerNextMs = now + 250;
        } else if (_buzzerStep == 1) {
            tone(HardwareConfig::buzzerPin, 1500, 100);
            _buzzerStep = 2; _buzzerNextMs = now + 100;
        } else {
            _buzzerPattern = 0; _buzzerStep = 0;
        }
    } else if (_buzzerPattern == 4) {  // SHUTDOWN: single low beep then stop
        tone(HardwareConfig::buzzerPin, 900, 400);
        _buzzerPattern = 0; _buzzerStep = 0;
        _buzzerNextMs  = now + 400;
    }
}

// ── Tool timers (STANDBY only) ────────────────────────────────
static unsigned long _fuelPrimeUntilMs      = 0;
static unsigned long _oilPrimeUntilMs       = 0;
static unsigned long _ignTestUntilMs        = 0;
static unsigned long _ign2TestUntilMs       = 0;
static unsigned long _startTestUntilMs      = 0;
static unsigned long _idleTestUntilMs       = 0;
static unsigned long _oilScavTestUntilMs    = 0;
static unsigned long _coolFanTestUntilMs    = 0;
static unsigned long _airstarterTestUntilMs = 0;
static unsigned long _bleedValveTestUntilMs = 0;
static unsigned long _glowTestUntilMs       = 0;
static unsigned long _fuelPump2TestUntilMs  = 0;
static unsigned long _abSolTestUntilMs      = 0;
static unsigned long _abPumpTestUntilMs     = 0;
static unsigned long _starterEnTestUntilMs  = 0;
static unsigned long _propPitchTestUntilMs  = 0;

static bool anyToolTimerActive() {
    // Also block actuator tests while extra cooldown is running — it controls the
    // starter, oil pump, and potentially other outputs that tests would conflict with.
    if (EngineData::instance().extraCooldownActive) return true;
    return _fuelPrimeUntilMs      || _oilPrimeUntilMs       ||
           _ignTestUntilMs        || _ign2TestUntilMs        ||
           _startTestUntilMs      ||
           _idleTestUntilMs       || _oilScavTestUntilMs     ||
           _coolFanTestUntilMs    || _airstarterTestUntilMs  ||
           _bleedValveTestUntilMs || _glowTestUntilMs        ||
           _propPitchTestUntilMs  ||
           _fuelPump2TestUntilMs  || _abSolTestUntilMs       ||
           _abPumpTestUntilMs     || _starterEnTestUntilMs;
}

// ── Relight state ─────────────────────────────────────────────
// Igniter held ON while relight criteria hold (flame gone, N1 above min, RUNNING).
// Cleared when: flame returns, N1 drops below min, or mode leaves RUNNING.
static bool          _relightActive    = false;

static void checkToolTimers() {
    if (EngineData::instance().mode != SysMode::STANDBY) return;
    auto& ed = EngineData::instance();
    unsigned long now = millis();
    if (_fuelPrimeUntilMs && now >= _fuelPrimeUntilMs)  { ed.fuelSolOpen   = false; _fuelPrimeUntilMs = 0; }
    if (_oilPrimeUntilMs  && now >= _oilPrimeUntilMs)   {
        // Hand off to standby feed level if windmill protection is still active,
        // otherwise zero. Without this, a 100 % prime demand would stay latched
        // after expiry whenever the standby feed was running concurrently.
        ed.oilPumpPct = ed.standbyOilFeedActive ? Config::standbyOilFeedPct : 0.0f;
        _oilPrimeUntilMs = 0;
    }
    if (_ignTestUntilMs   && now >= _ignTestUntilMs)     { ed.igniterOn      = false; _ignTestUntilMs   = 0; }
    if (_ign2TestUntilMs  && now >= _ign2TestUntilMs)    { ed.igniter2On     = false; _ign2TestUntilMs  = 0; }
    if (_startTestUntilMs && now >= _startTestUntilMs)   { ed.starterDemand = 0; ed.starterEnabled = false; _startTestUntilMs = 0; }
    if (_idleTestUntilMs  && now >= _idleTestUntilMs)    { ed.throttleDemand = 0;    _idleTestUntilMs  = 0; }
    if (_oilScavTestUntilMs    && now >= _oilScavTestUntilMs)    { ed.oilScavengeOn  = false; _oilScavTestUntilMs    = 0; }
    if (_coolFanTestUntilMs    && now >= _coolFanTestUntilMs)    { ed.coolFanOn       = false; _coolFanTestUntilMs    = 0; }
    if (_airstarterTestUntilMs && now >= _airstarterTestUntilMs) { ed.airstarterOpen  = false; _airstarterTestUntilMs = 0; }
    if (_bleedValveTestUntilMs && now >= _bleedValveTestUntilMs) { ed.bleedValveOpen  = false; _bleedValveTestUntilMs = 0; }
    if (_glowTestUntilMs       && now >= _glowTestUntilMs)       { ed.glowPlugDemand  = 0.0f;  _glowTestUntilMs       = 0; }
    if (_fuelPump2TestUntilMs  && now >= _fuelPump2TestUntilMs)  { ed.fuelPump2Demand = 0.0f;  _fuelPump2TestUntilMs  = 0; }
    if (_abSolTestUntilMs      && now >= _abSolTestUntilMs)      { ed.abSolOpen       = false; _abSolTestUntilMs      = 0; }
    if (_abPumpTestUntilMs     && now >= _abPumpTestUntilMs)     { ed.abPumpDemand  = 0.0f;  _abPumpTestUntilMs     = 0; }
    if (_starterEnTestUntilMs  && now >= _starterEnTestUntilMs)  { ed.starterEnabled  = false; _starterEnTestUntilMs  = 0; }
    if (_propPitchTestUntilMs  && now >= _propPitchTestUntilMs)  { ed.propPitchDemand = 0;     _propPitchTestUntilMs  = 0; }
}

// ── Extra Cooldown monitor ────────────────────────────────────
// Runs while extraCooldownActive.  Stops when:
//   - Mode leaves STANDBY (e.g. START command cancels it)
//   - User-set timeout expires (iParam seconds from slider)
static void checkExtraCooldown() {
    auto& ed = EngineData::instance();
    if (!ed.extraCooldownActive) return;

    // Guard: cancel if mode changed
    if (ed.mode != SysMode::STANDBY) {
        ed.extraCooldownActive = false;
        ed.starterDemand       = 0;
        ed.starterEnabled      = false;
        ed.oilPumpPct        = 0;
        ed.extraCooldownUntilMs  = 0;
        return;
    }

    if (ed.extraCooldownUntilMs && millis() >= ed.extraCooldownUntilMs) {
        ed.extraCooldownActive = false;
        ed.starterDemand       = 0;
        ed.starterEnabled      = false;
        ed.oilPumpPct        = 0;
        ed.extraCooldownUntilMs  = 0;
        Serial.println("[OT] Extra cooldown complete (timeout)");
    }
}

// ── Relight monitor ───────────────────────────────────────────
// Keeps igniter ON continuously while relight criteria hold.
// Fuel stays open (engine was RUNNING) — SafetyMonitor detects re-ignition.
// Igniter type (relay = full-on / PWM = dwell pattern) is handled by Hardware layer.
static void checkRelight() {
    if (!_relightActive) return;
    auto& ed = EngineData::instance();

    // Engine left RUNNING state — abort cleanly
    if (ed.mode != SysMode::RUNNING) {
        _relightActive  = false;
        ed.igniterOn    = false;
        return;
    }
    // Success: flame returned — turn igniter off, SafetyMonitor will clear flameout timer
    if (ed.flameDetected) {
        _relightActive  = false;
        ed.igniterOn    = false;
        Serial.println("[OT] Relight successful — flame detected");
        return;
    }
    // Abort: N1 dropped below minimum — engine winding down, stop trying.
    // Only abort on RPM if the sensor is healthy; a sensor fault returning
    // stale-zero would otherwise kill a valid relight attempt.
    if (ed.n1Healthy && ed.n1Rpm < Config::relightMinRpm) {
        _relightActive  = false;
        ed.igniterOn    = false;
        Serial.printf("[OT] Relight aborted — N1 below min (%.0f < %.0f)\n",
            (double)ed.n1Rpm, (double)Config::relightMinRpm);
        return;
    }
    // Criteria still met — keep igniter on continuously
    ed.igniterOn = true;
}

// ── Standby oil feed (windmill protection) ───────────────────
// When engine is windmilling in STANDBY (N1 above threshold from wind/momentum),
// run oil pump at a low feed duty to protect bearings.

static void checkStandbyOilFeed() {
    auto& hw = HardwareConfig::instance();
    if (!hw.hasOilPump || !hw.hasN1Rpm) return;
    auto& ed = EngineData::instance();
    if (ed.mode != SysMode::STANDBY) {
        ed.standbyOilFeedActive = false;
        return;
    }
    if (ed.extraCooldownActive) return;  // extra cooldown controls oil in standby

    if (ed.n1Rpm >= Config::standbyOilRpmLimit) {
        if (!ed.standbyOilFeedActive) {
            ed.standbyOilFeedActive = true;
            Serial.printf("[OT] Standby oil feed ON (N1=%.0f)\n", (double)ed.n1Rpm);
        }
        // Only set if no other tool is using the oil (prime etc.)
        if (ed.oilPumpPct < Config::standbyOilFeedPct) {
            ed.oilPumpPct = Config::standbyOilFeedPct;
        }
    } else if (ed.standbyOilFeedActive) {
        ed.standbyOilFeedActive = false;
        // Only zero demand if we were the highest bidder — don't cut a running oil prime.
        // Use <= so that demand exactly at our level (which we set) is cleared.
        // Demand above our level means another tool owns it; leave it alone.
        if (ed.oilPumpPct <= Config::standbyOilFeedPct) {
            ed.oilPumpPct = 0;
        }
        Serial.println("[OT] Standby oil feed OFF");
    }
}

// ── General-purpose DI channel polling ───────────────────────
// Debounces each configured DI channel and fires role actions on rising edge.
// SysMode enum bit positions: STANDBY=0, STARTUP=1, RUNNING=2, SHUTDOWN=3, FAULT=4
static void checkGeneralDI() {
    auto& hw = HardwareConfig::instance();
    auto& ed = EngineData::instance();
    unsigned long now = millis();

    for (int i = 0; i < HardwareConfig::MAX_DI; i++) {
        if (hw.diCh[i].pin < 0) continue;
        if (strcmp(hw.diCh[i].role, "none") == 0) continue;

        bool rawActive = (digitalRead(hw.diCh[i].pin) == (hw.diCh[i].activeH ? HIGH : LOW));

        // Debounce: only commit a change if the raw state has been stable for debounceMs
        if (rawActive != _diRawLast[i]) {
            _diLastChange[i] = now;
            _diRawLast[i]    = rawActive;
        }

        bool prevState = ed.diState[i];
        if ((now - _diLastChange[i]) >= (unsigned long)hw.diCh[i].debounceMs) {
            ed.diState[i] = rawActive;
        }

        // Rising edge: channel just became active
        if (ed.diState[i] && !prevState) {
            // Check activeModes bitmask
            uint8_t modeBit = (uint8_t)(1u << (int)ed.mode);
            if (!(hw.diCh[i].activeModes & modeBit)) continue;

            const char* role = hw.diCh[i].role;

            // Fault and estop roles must never fire in STANDBY or SHUTDOWN.
            // activeModes defaults to 0xFF (all modes) when not configured, so
            // without this guard any electrical noise on a DI pin in STANDBY
            // would trigger a fault shutdown, blocking normal engine start.
            if (ed.mode == SysMode::STANDBY || ed.mode == SysMode::SHUTDOWN) {
                if (strcmp(role, "fault") == 0 || strcmp(role, "estop") == 0) continue;
            }

            if (strcmp(role, "fault") == 0) {
                // Replicate SafetyMonitor fault path:
                // set faultDescription with user message, then trigger shutdown
                if (hw.diCh[i].faultMsg[0]) {
                    strncpy(ed.faultDescription, hw.diCh[i].faultMsg,
                            sizeof(ed.faultDescription) - 1);
                    ed.faultDescription[sizeof(ed.faultDescription) - 1] = '\0';
                } else if (hw.diCh[i].faultCode[0]) {
                    snprintf(ed.faultDescription, sizeof(ed.faultDescription),
                             "DI fault: %s", hw.diCh[i].faultCode);
                } else {
                    snprintf(ed.faultDescription, sizeof(ed.faultDescription),
                             "DI fault: channel %d triggered", i + 1);
                }
                // Inject the DI fault code into SafetyMonitor so that
                // enterFaultShutdown() reads the correct string via lastFault().
                // Without this, lastFault() returns null (or a stale code from a
                // previous safety fault), corrupting the flight log and lastEvent.
                const char* diCode = hw.diCh[i].faultCode[0] ? hw.diCh[i].faultCode : "DI_FAULT";
                g_safety.setExternalFault(diCode);
                Serial.printf("[DI] ch%d fault role triggered: %s\n", i, diCode);
                enterFaultShutdown();

            } else if (strcmp(role, "estop") == 0) {
                if (ed.mode == SysMode::RUNNING || ed.mode == SysMode::STARTUP) {
                    strncpy(ed.lastEvent, "Emergency stop — DI channel", sizeof(ed.lastEvent) - 1);
                    Serial.printf("[DI] ch%d estop role triggered\n", i);
                    enterShutdown();
                }

            } else if (strcmp(role, "ab_arm") == 0) {
                ed.abArmSwitchOn = true;
                Serial.printf("[DI] ch%d ab_arm active\n", i);

            } else if (strcmp(role, "limp_mode") == 0) {
                ed.limpMode = true;
                Serial.printf("[DI] ch%d limp_mode activated\n", i);

            } else if (strcmp(role, "ab_fire") == 0) {
                // Trigger AB fire — same effect as pressing AB FIRE button in the UI.
                // Conditions checked in handleCommand(AB_FIRE); push is safe from Core 1.
                CommandQueue::push({OTCommand::AB_FIRE});
                Serial.printf("[DI] ch%d ab_fire triggered\n", i);
            }
            // "inhibit_start" role: state is stored in ed.diState[i] and checked in handleCommand(START)
        }

        // Falling edge: level-sensitive roles that clear on release
        if (!ed.diState[i] && prevState) {
            const char* role = hw.diCh[i].role;
            if (strcmp(role, "ab_arm") == 0) {
                ed.abArmSwitchOn = false;
                Serial.printf("[DI] ch%d ab_arm inactive\n", i);
            } else if (strcmp(role, "limp_mode") == 0) {
                ed.limpMode = false;
                Serial.printf("[DI] ch%d limp_mode deactivated\n", i);
            }
        }
    }
}

// ── Afterburner state machine ─────────────────────────────────
// Separate parallel state machine running alongside main engine loop.
// AB can only run in RUNNING mode. Trigger sources: manual (web),
// throttle threshold, dedicated switch, or analog/RC input.

static bool _abInShutSeq = false;   // true while running ab shutdown sequence

static void enterABIgniting() {
    auto& ed = EngineData::instance();
    if (!HardwareConfig::hasAfterburner) return;
    if (ed.mode != SysMode::RUNNING) return;
    if (ed.abMode != ABMode::Off && ed.abMode != ABMode::Arming) return;

    ed.abMode = ABMode::Igniting;
    _abInShutSeq = false;
    FlightRecorder::logBlockEnter("AB_IGN_START");
    Serial.println("[AB] Entering ignition sequence");

    // Default sequence if nothing configured:
    //   ABCheckReady → ABSolOpen → ABPumpOn → ABIgnite(torch) → ABFlameConfirm → ABStabilize
    if (_abIgnCount == 0) {
        static IBlock* _defAbBlocks[] = {
            &g_blkABCheckReady, &g_blkABSolOpen, &g_blkABPumpOn,
            &g_blkABIgnite, &g_blkABFlameConfirm, &g_blkABStabilize
        };
        g_abSequencer.startSequence(_defAbBlocks, 6);
    } else {
        g_abSequencer.startSequence(_abIgnBlocks, _abIgnCount);
    }
}

static void enterABShutdown() {
    auto& ed = EngineData::instance();
    if (ed.abMode == ABMode::Off || ed.abMode == ABMode::ShuttingDown) return;
    ed.abMode     = ABMode::ShuttingDown;
    _abInShutSeq  = true;

    // Cut main fuel offset immediately — don't wait for the shutdown sequence
    ed.abFuelOffset = 0.0f;
    // Cut igniter immediately
    ed.igniter2On = false;

    Serial.println("[AB] Entering shutdown sequence");
    if (_abShutCount == 0) {
        // Default: close solenoid then cut pump — AB flame dies immediately
        static IBlock* _defAbShut[] = { &g_blkABSolClose, &g_blkABPumpOff };
        g_abSequencer.startSequence(_defAbShut, 2);
    } else {
        g_abSequencer.startSequence(_abShutBlocks, _abShutCount);
    }
}

// Called when AB ignition sequence completes (g_abSequencer done callback)
static void abSequenceDone() {
    auto& ed = EngineData::instance();
    if (_abInShutSeq) {
        // Shutdown sequence done
        ed.abMode         = ABMode::Off;
        ed.abSolOpen      = false;
        ed.abPumpDemand = 0;
        ed.igniter2On     = false;
        _abInShutSeq      = false;
        Serial.println("[AB] Shutdown complete — AB Off");
    }
    // If ignition seq done: abMode was set to Running by ABStabilize.onExit()
}

static void abSequenceAbort() {
    auto& ed = EngineData::instance();
    ed.abSolOpen      = false;
    ed.abPumpDemand = 0;
    ed.abFuelOffset   = 0.0f;
    ed.igniter2On     = false;
    if (_abInShutSeq) {
        // Shutdown sequence aborted — treat as complete; AB is off
        ed.abMode    = ABMode::Off;
        _abInShutSeq = false;
        Serial.println("[AB] Shutdown sequence aborted — AB Off");
    } else {
        // Ignition sequence aborted (e.g. ABCheckReady conditions not met).
        // Set Fault rather than Off so checkABTrigger() doesn't immediately
        // re-enter the ignition sequence on the next tick while the trigger
        // is still asserted — which would create a rapid re-entry loop.
        // User must release and re-assert the trigger to retry.
        ed.abMode = ABMode::Fault;
        Serial.println("[AB] Ignition sequence aborted — requires trigger release to retry");
    }
}

static void abSequenceFault() {
    auto& ed = EngineData::instance();
    ed.abMode         = ABMode::Fault;
    ed.abSolOpen      = false;
    ed.abPumpDemand = 0;
    ed.abFuelOffset   = 0.0f;
    ed.igniter2On     = false;
    _abInShutSeq      = false;
    Serial.println("[AB] Sequence FAULT — ignition failed");
    // Don't fault the main engine; AB fault is non-critical
    // Leave abMode=Fault until next start attempt
}

static void checkABTrigger() {
    if (!HardwareConfig::hasAfterburner) return;
    auto& ed  = EngineData::instance();
    auto& hw  = HardwareConfig::instance();

    // Only active in RUNNING mode
    if (ed.mode != SysMode::RUNNING) {
        if (ed.abMode != ABMode::Off) {
            enterABShutdown();
        }
        return;
    }

    // If AB is running and main engine shuts down, close AB
    // (handled above)

    // ── Evaluate trigger ─────────────────────────────────────
    bool triggerAsserted = false;

    switch (hw.abTriggerSource) {
        case 0: // manual only — no automatic trigger polling
            break;

        case 1: // throttle threshold
            // Read ThrottleSlew's clean output — NOT ed.throttleDemand, which may
            // already have the AB fuel offset baked in from the previous tick.
            // Using the offset-inflated value would prevent shutdown when the pilot
            // drops throttle below the threshold (offset keeps it above it).
            triggerAsserted = (g_ctrlThrottleSlew.currentDemand() >= Config::abThrottleThreshold);
            break;

        case 2: // dedicated switch
            if (hw.abSwitchPin >= 0) {
                triggerAsserted = (digitalRead(hw.abSwitchPin) ==
                                   (hw.abSwitchActiveH ? HIGH : LOW));
            }
            break;

        case 3: // analog / RC input
            triggerAsserted = (ed.abInputRaw >= hw.abInputThreshold);
            break;
    }

    ed.abTriggerActive = triggerAsserted;

    // Apply arm switch gate (source 1/2/3 only; not manual)
    if (hw.abTriggerSource != 0 && hw.abRequiresArmSwitch) {
        if (!ed.abArmSwitchOn) triggerAsserted = false;
    }

    // ── State transitions ────────────────────────────────────
    // Rising-edge latch: only re-enter from Off/Fault on a fresh trigger assertion.
    // Without this, a Fault set while the trigger is still held causes an immediate
    // re-entry on the very next tick — creating the same rapid loop as Off did.
    static bool _abTriggerPrev = false;
    bool triggerRisingEdge = triggerAsserted && !_abTriggerPrev;

    switch (ed.abMode) {
        case ABMode::Off:
        case ABMode::Fault:
            if (triggerRisingEdge && hw.abTriggerSource != 0) {
                enterABIgniting();
            }
            break;

        case ABMode::Running:
            // AB main fuel offset: stored in ed.abFuelOffset and applied at the
            // actuator write in Hardware::updateActuators().  Do NOT add it to
            // throttleDemand — that value is ThrottleSlew's input/output and
            // writing an inflated value there causes the slew to drift upward
            // (toward throttleDemand, which is already offset) every tick.
            ed.abFuelOffset = Config::abMainFuelOffsetPct / 100.0f;
            // Apply AB pump demand: follow throttle (lerp min→max) or fixed at max.
            // Use ThrottleSlew's clean output so the AB pump tracks true pilot demand.
            {
                float pct;
                if (Config::abPumpFollowThrottle) {
                    pct = Config::abPumpMinPct + (Config::abPumpMaxPct - Config::abPumpMinPct)
                          * g_ctrlThrottleSlew.currentDemand();
                } else {
                    pct = Config::abPumpMaxPct;
                }
                ed.abPumpDemand = constrain(pct / 100.0f, 0.0f, 1.0f);
            }
            // Shut down if trigger released
            if (!triggerAsserted && hw.abTriggerSource != 0) {
                enterABShutdown();
            }
            break;

        case ABMode::Igniting:
        case ABMode::Arming:
        case ABMode::ShuttingDown:
            break;  // sequencer is running — let it finish
    }

    _abTriggerPrev = triggerAsserted;
}

// ── Cooldown skip (hold START+STOP in SHUTDOWN) ───────────────
// Holding both buttons simultaneously for cooldownSkipHoldMs
// while in SHUTDOWN mode aborts cooldown and goes directly to STANDBY.
static unsigned long _cooldownSkipHoldStart = 0;

static void checkCooldownSkip() {
    auto& ed = EngineData::instance();
    if (ed.mode != SysMode::SHUTDOWN) {
        _cooldownSkipHoldStart = 0;
        return;
    }
    auto& hcfg = HardwareConfig::instance();
    bool startHeld = (digitalRead(hcfg.startPin) == (hcfg.startActiveH ? HIGH : LOW));
    bool stopHeld  = (digitalRead(hcfg.stopPin)  == (hcfg.stopActiveH  ? HIGH : LOW));
    if (startHeld && stopHeld) {
        if (_cooldownSkipHoldStart == 0) _cooldownSkipHoldStart = millis();
        else if ((millis() - _cooldownSkipHoldStart)
                 >= (unsigned long)Config::cooldownSkipHoldMs)
        {
            _cooldownSkipHoldStart = 0;
            Serial.println("[OT] Cooldown skip — both buttons held");
            strncpy(ed.lastEvent, "Cooldown skipped by operator", sizeof(ed.lastEvent) - 1);
            enterStandby();
        }
    } else {
        _cooldownSkipHoldStart = 0;
    }
}

// ── Starter assist (RUNNING only) ────────────────────────────
// When enabled: keeps the starter motor spinning at a low % while N1 is below
// starterAssistExitRpm, giving the engine torque support at low idle.
// Exits (and re-arms) via hysteresis: re-enables only below starterAssistExitRpm * 0.5.
static void checkStarterAssist() {
    auto& hw = HardwareConfig::instance();
    if (!hw.hasStarter) return;
    if (!hw.starterAssistEnabled) return;    // disabled in hardware config
    auto& ed = EngineData::instance();

    // Hysteresis state: set when N1 climbs above exitRpm, cleared when N1 drops back
    // below 50% of exitRpm.  Reset to false whenever the assist is freshly armed so
    // the first engagement always works without requiring the 50% drop.
    static bool _saDisengaged = false;
    static bool _prevRunning  = false;
    if (!_prevRunning || !ed.starterAssistActive) _saDisengaged = false;
    _prevRunning = (ed.mode == SysMode::RUNNING && ed.starterAssistActive);

    if (ed.mode != SysMode::RUNNING) return;
    if (!ed.starterAssistActive) return;

    // If the RPM sensor is unhealthy, stale/zero n1Rpm could either lock the
    // starter on permanently or drop it when the engine still needs support.
    // Disengage safely until the sensor recovers.
    if (!ed.n1Healthy) {
        ed.starterEnabled = false;
        ed.starterDemand  = 0;
        return;
    }

    // Hysteresis: disengage at exitRpm, re-arm only when N1 drops below 50% of exitRpm.
    // Prevents rapid on/off chattering if RPM oscillates around the threshold.
    if (ed.n1Rpm >= Config::starterAssistExitRpm) {
        _saDisengaged = true;
    } else if (ed.n1Rpm < Config::starterAssistExitRpm * 0.5f) {
        _saDisengaged = false;
    }

    if (!_saDisengaged) {
        ed.starterEnabled = true;
        ed.starterDemand  = Config::starterAssistPct / 100.0f;
    } else {
        // N1 is in the dead-band (50–100 % of exit) — hold off until it drops further.
        ed.starterEnabled = false;
        ed.starterDemand  = 0;
    }
}

// ── Mode transitions ──────────────────────────────────────────

static void enterRunning() {
    auto& ed = EngineData::instance();
    ed.mode               = SysMode::RUNNING;
    ed.throttleDemand     = 0;    // clear ModifiedIdle/Spool demand; throttle controller takes over
    // Dev mode and bench mode runs are not real engine starts — don't count toward run log
    if (!ed.benchMode && !ed.devMode) ed.runCount = ed.runCount + 1;
    ed.relightArmed       = true;   // arm relight for this run
    ed.relightAttempts    = 0;      // reset attempt counter
    // Ensure flameout detection is armed regardless of which startup sequence was
    // used.  Spool::onEnter() normally sets this, but custom sequences that omit
    // Spool would silently leave flameMonitorActive=false and flameout would
    // never be detected in RUNNING mode.
    ed.flameMonitorActive = true;
    _runStartMs        = millis();
    strncpy(ed.lastEvent, "Startup complete — engine self-sustained", sizeof(ed.lastEvent) - 1);
    _buzzerPattern = 2;  // startup OK beep
    Hardware::initControllers();
    FlightRecorder::logRunningEntry();
    Serial.println("[OT] RUNNING");
}

static void enterShutdown() {
    auto& ed = EngineData::instance();
    if (ed.mode == SysMode::SHUTDOWN) return;  // already shutting down
    ed.mode = SysMode::SHUTDOWN;
    _buzzerPattern = 4; _buzzerStep = 0;  // single low beep: normal stop
    // Clear operator-hold states so igniter/flags don't persist into cooldown
    ed.manualRelightActive = false;
    ed.igniterOn           = false;
    strncpy(ed.lastEvent, "Normal shutdown commanded", sizeof(ed.lastEvent) - 1);
    FlightRecorder::logNormalShutdown();
    g_sequencer.startSequence(_shutdownBlocks, _shutdownCount);
    Serial.println("[OT] SHUTDOWN");
}

static void enterFaultShutdown() {
    auto& ed = EngineData::instance();
    const char* fault = g_safety.lastFault();
    FlightRecorder::logFault(fault);           // sensor snapshot at moment of fault
    FlightRecorder::logFaultShutdown(fault);   // shutdown event record
    ed.mode = SysMode::SHUTDOWN;
    // Synchronously stop any active AB sequence so igniter2, solenoid and
    // AB pump are cut immediately rather than waiting for the next
    // checkABTrigger() tick.
    if (HardwareConfig::hasAfterburner) enterABShutdown();
    snprintf(ed.lastEvent, sizeof(ed.lastEvent), "FAULT: %s", fault);
    _buzzerPattern = 1;  // rapid fault beep
    g_sequencer.startSequence(_shutdownBlocks, _shutdownCount);
    Serial.printf("[OT] FAULT SHUTDOWN: %s\n", fault);
    if (HardwareConfig::hasClusterSerial) {
        // Send fault-specific cluster status code (more descriptive than generic ShuttingDown)
        if      (strcmp(fault, "OVERSPEED") == 0)   ClusterSerial::sendStatus(ClCode::Overspeed);
        else if (strcmp(fault, "FLAMEOUT")  == 0)   ClusterSerial::sendStatus(ClCode::FlameOut);
        else if (strcmp(fault, "LOW_OIL")   == 0)   ClusterSerial::sendStatus(ClCode::OilPressureLow);
        else if (strcmp(fault, "OIL_ZERO")  == 0)   ClusterSerial::sendStatus(ClCode::OilZero);
        else                                         ClusterSerial::sendStatus(ClCode::ShuttingDown);
    }
    if (HardwareConfig::hasMAVLink) {
        char buf[50];
        snprintf(buf, sizeof(buf), "FAULT: %s", fault ? fault : "?");
        g_mavlink.sendStatusText(buf);
    }
}

static void enterStandby() {
    auto& ed = EngineData::instance();
    SessionLogger::endSession();   // close session log for this run
    // Accumulate engine-on time (only if we actually entered RUNNING this session)
    if (_runStartMs > 0) {
        // Bench / dev mode runs are not real engine time — don't count toward total
        if (!ed.benchMode && !ed.devMode) {
            uint32_t elapsed = (millis() - _runStartMs) / 1000;
            Config::totalRunSeconds += elapsed;
            Config::requestSave();   // deferred to Core 0 — no LittleFS I/O on Core 1
        }
        _runStartMs = 0;
    }
    _buzzerPattern = 0;  // silence any buzzer
    ed.mode               = SysMode::STANDBY;
    ed.throttleDemand     = 0;
    ed.propPitchDemand    = 0;
    ed.abPumpDemand       = 0;
    ed.fuelPump2Demand    = 0;
    ed.oilTargetBar          = 0;
    ed.oilPumpPct       = 0;      // clear pump % — prevents stuck-at-failsafe in standby
    ed.oilFailsafeActive  = false;
    ed.fuelSolOpen        = false;
    ed.igniterOn          = false;
    ed.starterDemand      = 0;
    ed.starterEnabled     = false;
    ed.starterAssistActive = false;
    ed.manualRelightActive = false;
    ed.flameMonitorActive = false;
    ed.oilMinBar          = 0;
    ed.relightArmed       = false;
    ed.relightAttempts    = 0;
    ed.extraCooldownActive = false;
    ed.extraCooldownUntilMs  = 0;
    _ign2TestUntilMs       = 0;
    _oilScavTestUntilMs    = 0;
    _coolFanTestUntilMs    = 0;
    _airstarterTestUntilMs = 0;
    _bleedValveTestUntilMs = 0;
    _glowTestUntilMs       = 0;
    _fuelPump2TestUntilMs  = 0;
    _abSolTestUntilMs      = 0;
    _abPumpTestUntilMs     = 0;
    _starterEnTestUntilMs  = 0;
    _propPitchTestUntilMs  = 0;
    _relightActive         = false;
    ed.limpMode           = false;
    ed.clusterCode        = 0;
    ed.fuelEverOpened     = false;
    // AB cleanup
    if (g_abSequencer.isRunning()) g_abSequencer.stopSequence();
    ed.abMode          = ABMode::Off;
    ed.abSolOpen       = false;
    ed.abTriggerActive = false;
    _abInShutSeq       = false;
    if (ed.lastEvent[0] == 0) {
        strncpy(ed.lastEvent, "Ready", sizeof(ed.lastEvent) - 1);
    }
    ed.faultDescription[0] = '\0';  // clear fault banner from previous run
    Hardware::allOff();
    Serial.println("[OT] STANDBY");
}

static void enterAbortStandby() {
    auto& ed = EngineData::instance();
    const char* blockName = g_sequencer.currentBlockName();
    snprintf(ed.lastEvent, sizeof(ed.lastEvent), "Aborted at: %s", blockName);
    FlightRecorder::logAbort(blockName, "startup_abort");
    // Set a plain-language description for the fault/abort banner
    if (strcmp(blockName, "OilPrime") == 0 || strcmp(blockName, "OilPump") == 0) {
        strncpy(ed.faultDescription,
            "Startup aborted: oil prime did not reach target pressure in time.\n"
            "What to do: Check oil level, oil pump wiring and duty settings, "
            "and oil line connections. Try running Oil Prime from the Tools page to diagnose.",
            sizeof(ed.faultDescription) - 1);
    } else if (strcmp(blockName, "FlameConfirm") == 0) {
        strncpy(ed.faultDescription,
            "Startup aborted: flame was not detected within the allowed time.\n"
            "What to do: Check fuel supply, fuel solenoid, igniter operation, "
            "and flame sensor threshold. Try Igniter Test from the Tools page.",
            sizeof(ed.faultDescription) - 1);
    } else if (strcmp(blockName, "Spool") == 0) {
        strncpy(ed.faultDescription,
            "Startup aborted: engine did not reach spool RPM in time.\n"
            "What to do: Check starter motor, throttle calibration, and fuel flow. "
            "Increase spool timeout in Sequence settings if the engine is healthy.",
            sizeof(ed.faultDescription) - 1);
    } else {
        snprintf(ed.faultDescription, sizeof(ed.faultDescription),
            "Startup aborted at sequence step: %s.\n"
            "What to do: Check the Flight Log for details. "
            "Verify all sensors and actuators are working correctly.",
            blockName);
    }
    ed.faultDescription[sizeof(ed.faultDescription) - 1] = '\0';
    _buzzerPattern = 1;  // rapid fault beep

    if (ed.fuelEverOpened) {
        // Fuel was opened this attempt — engine may have partial combustion and hot EGT.
        // Run the full shutdown sequence (ImmediateCut → RPMDrop → CooldownSpin → FinalStop)
        // to keep bearings oiled through spindown and cool the turbine before standby.
        ed.mode = SysMode::SHUTDOWN;
        FlightRecorder::logFaultShutdown("STARTUP_ABORT");
        g_sequencer.startSequence(_shutdownBlocks, _shutdownCount);
        Serial.printf("[OT] Startup abort (fuel was open) → SHUTDOWN for safe spindown\n");
    } else {
        // Aborted before any ignition attempt — safe to go directly to STANDBY.
        enterStandby();
    }
}

// ── Sequence complete dispatcher ──────────────────────────────
// The sequencer uses a single Complete callback for both startup and shutdown.
// We check current mode to decide which transition to make.
static void sequenceComplete() {
    if (EngineData::instance().mode == SysMode::STARTUP) {
        enterRunning();   // startup finished successfully → RUNNING
    } else {
        enterStandby();   // shutdown finished → STANDBY
    }
}

// ── Command handler (called from ECU loop on Core 1) ─────────

static void handleCommand(const OTPacket& pkt) {
    auto& ed = EngineData::instance();

    switch (pkt.cmd) {
        case OTCommand::START:
            if (ed.mode == SysMode::STANDBY && Config::profileMatch) {
                // Check inhibit_start DI channels
                {
                    auto& hwi = HardwareConfig::instance();
                    bool inhibited = false;
                    for (int _i = 0; _i < HardwareConfig::MAX_DI; _i++) {
                        if (hwi.diCh[_i].pin >= 0
                            && strcmp(hwi.diCh[_i].role, "inhibit_start") == 0
                            && ed.diState[_i])
                        {
                            Serial.printf("[OT] START inhibited by DI ch%d (%s)\n",
                                          _i, hwi.diCh[_i].label[0] ? hwi.diCh[_i].label : "inhibit_start");
                            inhibited = true;
                            break;
                        }
                    }
                    if (inhibited) break;
                }
                // Block start if sequence has errors (e.g. missing required sensors).
                // Bench mode bypasses this so hardware-less testing is still possible.
                if (ed.seqHasErrors && !ed.benchMode) {
                    Serial.println("[OT] START blocked: sequence has hardware errors — enable bench mode to override");
                    strncpy(ed.faultDescription,
                            "Cannot start: sequence requires hardware that is not configured. "
                            "Check Sequence page for details, or enable Bench Mode to override.",
                            sizeof(ed.faultDescription) - 1);
                    break;
                }
                ed.mode = SysMode::STARTUP;
                _buzzerPattern = 3; _buzzerStep = 0;  // double chirp: sequence starting
                ed.faultDescription[0] = '\0';  // clear previous fault/abort description
                strncpy(ed.lastEvent, "Start sequence initiated", sizeof(ed.lastEvent) - 1);
                Hardware::applyConfig();  // re-apply config before each start
                FlightRecorder::logStartAttempt();
                SessionLogger::startSession();  // open new session CSV for this run
                g_sequencer.startSequence(_startupBlocks, _startupCount);
                Serial.println("[OT] START commanded");
            }
            break;

        case OTCommand::STOP:
            if (ed.mode == SysMode::RUNNING || ed.mode == SysMode::STARTUP) {
                enterShutdown();
            } else if (ed.mode == SysMode::SHUTDOWN) {
                // Already shutting down — do nothing
            }
            break;

        case OTCommand::TOGGLE_DYNAMIC_IDLE:
            ed.dynamicIdleEnabled = !ed.dynamicIdleEnabled;
            break;

        case OTCommand::TOGGLE_LIMP_MODE:
            ed.limpMode = !ed.limpMode;
            break;

        case OTCommand::TOGGLE_SAFETY_CHECKS:
            if (ed.devMode) ed.skipSafetyChecks = !ed.skipSafetyChecks;
            break;

        case OTCommand::TOGGLE_DEV_MODE:
            ed.devMode = !ed.devMode;
            if (!ed.devMode) {
                // Disabling dev mode also clears all dev-only overrides
                ed.skipSafetyChecks = false;
                ed.benchMode        = false;
            }
            Serial.printf("[OT] Dev mode %s\n", ed.devMode ? "ENABLED" : "disabled");
            break;

        case OTCommand::TOGGLE_BENCH_MODE:
            // Bench mode only active in dev mode and only changeable in STANDBY
            if (ed.devMode && ed.mode == SysMode::STANDBY) {
                ed.benchMode = !ed.benchMode;
                Serial.printf("[OT] Bench mode %s\n", ed.benchMode ? "ENABLED — safety/sensor waits bypassed" : "disabled");
            }
            break;

        case OTCommand::SET_OIL_DEMAND:
            if (ed.mode == SysMode::STANDBY || ed.devMode) {
                ed.oilTargetBar = constrain(pkt.fParam, 0.0f, 20.0f);  // bar; 20 is well above any real turbine oil pressure
            }
            break;

        case OTCommand::SET_OIL_PCT:
            // Always allow in STANDBY (oil failsafe manual override), also in devMode during run
            if (ed.mode == SysMode::STANDBY || ed.devMode) {
                ed.oilPumpPct = (float)constrain(pkt.iParam, 0, 100);
            }
            break;

        case OTCommand::FUEL_PRIME:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.fuelSolOpen    = true;
                _fuelPrimeUntilMs = millis() + Config::toolFuelPrimeMs;
            }
            break;

        case OTCommand::OIL_PRIME:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.oilPumpPct  = 100.0f;
                _oilPrimeUntilMs = millis() + Config::toolOilPrimeMs;
            }
            break;

        case OTCommand::IGN_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.igniterOn     = true;
                _ignTestUntilMs  = millis() + Config::toolIgnTestMs;
            }
            break;

        case OTCommand::IGN2_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.igniter2On    = true;
                _ign2TestUntilMs = millis() + Config::toolIgnTestMs;
            }
            break;

        case OTCommand::START_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.starterEnabled  = true;
                ed.starterDemand   = 0.3f;
                _startTestUntilMs  = millis() + Config::toolStartTestMs;
            }
            break;

        case OTCommand::FUEL_SOL_TEST:
            // Brief solenoid pulse — audible click only, reuses fuel prime timer
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.fuelSolOpen    = true;
                _fuelPrimeUntilMs = millis() + Config::toolFuelSolTestMs;
            }
            break;

        case OTCommand::IDLE_TEST:
            // Move throttle servo to idle position for 3 s to verify mechanical travel
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.throttleDemand = Config::throttleIdleMinPct / 100.0f;
                _idleTestUntilMs  = millis() + 3000;
            }
            break;

        case OTCommand::EXTRA_COOLDOWN:
            if (ed.mode == SysMode::STANDBY) {
                if (!ed.extraCooldownActive && pkt.iParam > 0) {
                    // iParam = duration in seconds from UI slider (60–300 s)
                    unsigned long durationMs  = (unsigned long)pkt.iParam * 1000UL;
                    ed.extraCooldownActive    = true;
                    ed.oilFailsafeActive      = false;  // take manual control
                    ed.starterEnabled         = true;
                    ed.starterDemand          = 0.3f;
                    ed.oilPumpPct           = 30.0f;
                    ed.extraCooldownUntilMs     = millis() + durationMs;
                    Serial.printf("[OT] Extra cooldown started (%lu s)\n",
                        (unsigned long)pkt.iParam);
                } else {
                    // Cancel — either toggle-off or iParam == 0 (stop button)
                    ed.extraCooldownActive = false;
                    ed.oilFailsafeActive   = false;
                    ed.starterDemand       = 0;
                    ed.starterEnabled      = false;
                    ed.oilPumpPct        = 0;
                    ed.extraCooldownUntilMs  = 0;
                    Serial.println("[OT] Extra cooldown cancelled");
                }
            }
            break;

        case OTCommand::STARTER_ASSIST:
            // iParam: 0 = off, non-zero = on (pct comes from Config::starterAssistPct)
            ed.starterAssistActive = (pkt.iParam != 0);
            if (!ed.starterAssistActive) {
                ed.starterEnabled = false;
                ed.starterDemand  = 0;
            }
            break;

        case OTCommand::CLEAR_LOG:
            FlightRecorder::clear();
            break;

        case OTCommand::AB_FIRE:
            // Manual AB ignition — only allowed in RUNNING and if AB is off/fault
            if (HardwareConfig::hasAfterburner
                && ed.mode == SysMode::RUNNING
                && (ed.abMode == ABMode::Off || ed.abMode == ABMode::Fault))
            {
                Serial.println("[AB] Manual fire command received");
                enterABIgniting();
            }
            break;

        case OTCommand::AB_STOP:
            // Manual AB shutdown
            if (HardwareConfig::hasAfterburner
                && ed.abMode != ABMode::Off
                && ed.abMode != ABMode::ShuttingDown)
            {
                Serial.println("[AB] Manual stop command received");
                enterABShutdown();
            }
            break;

        case OTCommand::APPLY_CONFIG:
            // Re-apply block params from config — only safe in STANDBY.
            // Controller static values (gains, limits) are updated by Config::fromJson
            // in the PATCH handler immediately; applyConfig() copies them into block
            // instances and reinitialises actuator mappings.
            if (ed.mode == SysMode::STANDBY) {
                Hardware::applyConfig();
                Serial.println("[OT] APPLY_CONFIG: block params reloaded from config");
            } else {
                // In any other mode the command is deferred — config values are live
                // in memory but hardware block instances won't be updated until the
                // next STANDBY transition.  Log so this isn't a silent surprise.
                Serial.println("[OT] APPLY_CONFIG: deferred — not in STANDBY, hardware blocks update on next STANDBY");
            }
            break;

        // ── Actuator tests (STANDBY only, auto-expire via checkToolTimers) ────
        case OTCommand::OIL_SCAV_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.oilScavengeOn    = true;
                _oilScavTestUntilMs = millis() + 2000;
            }
            break;

        case OTCommand::COOL_FAN_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.coolFanOn         = true;
                _coolFanTestUntilMs  = millis() + 3000;
            }
            break;

        case OTCommand::AIRSTARTER_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.airstarterOpen       = true;
                _airstarterTestUntilMs  = millis() + 1000;
            }
            break;

        case OTCommand::BLEED_VALVE_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.bleedValveOpen       = true;
                _bleedValveTestUntilMs  = millis() + 1000;
            }
            break;

        case OTCommand::GLOW_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.glowPlugDemand = 0.5f;
                _glowTestUntilMs  = millis() + 3000;
            }
            break;

        case OTCommand::FUEL_PUMP2_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.fuelPump2Demand     = 0.3f;
                _fuelPump2TestUntilMs  = millis() + 3000;
            }
            break;

        case OTCommand::AB_SOL_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.abSolOpen      = true;
                _abSolTestUntilMs = millis() + 1000;
            }
            break;

        case OTCommand::AB_PUMP_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.abPumpDemand   = 0.3f;
                _abPumpTestUntilMs  = millis() + 2000;
            }
            break;

        case OTCommand::STARTER_EN_TEST:
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.starterEnabled      = true;
                _starterEnTestUntilMs  = millis() + 1000;
            }
            break;

        case OTCommand::PROP_PITCH_TEST:
            // Move prop pitch to mid-travel (0.5) for 3 s — verify servo range
            if (ed.mode == SysMode::STANDBY && !anyToolTimerActive()) {
                ed.propPitchDemand     = 0.5f;
                _propPitchTestUntilMs  = millis() + 3000;
            }
            break;

        case OTCommand::RESET_PEAKS:
            ed.maxN1           = 0;
            ed.maxN2           = 0;
            ed.maxTot          = 0;
            ed.maxTit          = 0;
            ed.maxP1           = 0;
            ed.maxP2           = 0;
            ed.maxOilTemp      = 0;
            ed.maxBattVoltage  = 0;
            ed.maxFuelPressure = 0;
            break;

        default:
            break;
    }
}

// ── Stop / Start switch polling ───────────────────────────────

static void checkStopSwitch() {
    auto& ed = EngineData::instance();
    auto& hc  = HardwareConfig::instance();
    bool active = (digitalRead(hc.stopPin) == (hc.stopActiveH ? HIGH : LOW));
    ed.stopSwitchActive = active;
    if (active) {
        if (ed.mode == SysMode::RUNNING || ed.mode == SysMode::STARTUP) {
            strncpy(ed.lastEvent, "Stop switch activated", sizeof(ed.lastEvent) - 1);
            enterShutdown();
        }
    }
}

static void checkStartSwitch() {
    // Edge-detect: normalise to active-low convention (cur==LOW means "pressed")
    // so all downstream logic is unchanged regardless of startActiveH.
    auto& hca = HardwareConfig::instance();
    int  rawLevel = digitalRead(hca.startPin);
    bool pressed  = hca.startActiveH ? (rawLevel == HIGH) : (rawLevel == LOW);
    // Represent as a synthetic LOW/HIGH for the _last comparison below
    int cur = pressed ? LOW : HIGH;
    static int _last = HIGH;  // start in "released" (HIGH) state
    auto& ed = EngineData::instance();
    ed.startSwitchActive = pressed;

    if (_last == HIGH && cur == LOW) {
        // Only send START command in STANDBY — in RUNNING the hold logic below handles it
        if (ed.mode == SysMode::STANDBY) {
            CommandQueue::push({ OTCommand::START });
        }
    }

    // Manual relight: operator holds START while RUNNING → force igniter on
    // Controlled by Config::igniterOnStart (configurable in Misc section)
    if (ed.mode == SysMode::RUNNING && Config::igniterOnStart) {
        if (cur == LOW) {
            if (!ed.manualRelightActive) {
                ed.manualRelightActive = true;
                ed.igniterOn           = true;
                Serial.println("[OT] Manual relight — START held");
            }
        } else if (ed.manualRelightActive) {
            ed.manualRelightActive = false;
            ed.igniterOn           = false;
            Serial.println("[OT] Manual relight — START released");
        }
    } else if (ed.mode != SysMode::RUNNING) {
        // Mode changed away from RUNNING (fault, shutdown) — cut igniter immediately
        // if it was lit by manual relight.  ImmediateCut will also clear it, but
        // doing it here avoids a one-frame gap.
        if (ed.manualRelightActive) ed.igniterOn = false;
        ed.manualRelightActive = false;
    }

    _last = cur;
}

// ── Web server task (Core 0) ──────────────────────────────────

static void webTask(void*) {
    WebServer::begin();
    for (;;) {
        WebServer::tick();
        // Give web more CPU in STANDBY; engine is priority when active
        SysMode m = EngineData::instance().mode;
        bool engineActive = (m != SysMode::STANDBY);
        vTaskDelay(pdMS_TO_TICKS(engineActive ? 20 : 5));
    }
}

// ── Arduino entry points ──────────────────────────────────────

void setup() {
    PlatformInit::begin();

    // Load hardware topology FIRST (pins, feature flags, sequence order).
    // Must be called after LittleFS is mounted (PlatformInit::begin() does that).
    HardwareConfig::load();
    buildSequences();

    // Re-init stop/start GPIO with runtime pins from hardware.json
    {
        auto& hcfg = HardwareConfig::instance();
        pinMode(hcfg.stopPin,  hcfg.stopPullup  ? INPUT_PULLUP : INPUT);
        pinMode(hcfg.startPin, hcfg.startPullup ? INPUT_PULLUP : INPUT);
    }

    Config::load();
    if (EngineData::instance().mode == SysMode::FAULT) {
        Serial.println("[OT] FAULT: profile ID mismatch — engine ops locked");
        // Web server still starts so user can see the error and fix config
    }

    // Cross-check: hardware.json and settings sections should share the same profile_id.
    // A divergence means the user edited one section independently. This is not fatal
    // (Config::profileMatch already governs engine ops), but worth a loud warning.
    if (HardwareConfig::profileId[0] != '\0'
        && Config::profileId[0] != '\0'
        && strcmp(HardwareConfig::profileId, Config::profileId) != 0)
    {
        Serial.printf("[OT] WARNING: hardware profile_id (%s) differs from settings profile_id (%s)"
                      " — update both sections to the same value\n",
                      HardwareConfig::profileId, Config::profileId);
    }

#ifdef OT_DEV_MODE
    EngineData::instance().devMode = true;
    Serial.println("[OT] DEV_MODE: enabled — config locks bypassed, NEVER ship this build");
#endif

    Hardware::applyConfig();

    FlightRecorder::begin();
    SessionLogger::begin();
    CommandQueue::begin();

    Hardware::initSensors();
    Hardware::initActuators();
    Hardware::initStatusLED();
    RCInput::begin();

    // ── DI pin mode + initial debounce state ──────────────────
    // Set pin mode for each configured DI channel and seed _diRawLast
    // from the actual current reading so the first loop() tick does not
    // produce a spurious rising-edge trigger on any channel that is
    // already active at power-up.
    {
        auto& hdi = HardwareConfig::instance();
        for (int i = 0; i < HardwareConfig::MAX_DI; i++) {
            if (hdi.diCh[i].pin < 0) continue;
            if (strcmp(hdi.diCh[i].role, "none") == 0) continue;
            // Active-low channels use pullup; active-high use floating input.
            pinMode(hdi.diCh[i].pin,
                    hdi.diCh[i].activeH ? INPUT : INPUT_PULLUP);
            bool state = (digitalRead(hdi.diCh[i].pin) ==
                          (hdi.diCh[i].activeH ? HIGH : LOW));
            _diRawLast[i]    = state;
            _diLastChange[i] = millis();
            // Also pre-seed EngineData so the first poll sees no change
            EngineData::instance().diState[i] = state;
        }
    }

    g_sequencer.setCallbacks(sequenceComplete, enterAbortStandby, enterFaultShutdown);
    g_abSequencer.setCallbacks(abSequenceDone, abSequenceAbort, abSequenceFault);
    g_safety.begin(enterShutdown, enterFaultShutdown);
    // Safety thresholds are applied via Hardware::applyConfig() above

    // Relight callback — fires on flameout if relightEnabled and conditions met.
    // Igniter stays ON continuously; checkRelight() turns it off when done.
    g_safety.setRelightCallback([]() {
        auto& ed = EngineData::instance();
        if (!_relightActive) {
            // First call for this flameout event — start continuous ignition.
            // SafetyMonitor owns the timeout; this callback just lights the igniter.
            ed.relightAttempts = ed.relightAttempts + 1;
            _relightActive     = true;
            ed.clusterCode     = 2;   // ClCode::RelightActive
            FlightRecorder::logRelight(ed.relightAttempts);
            Serial.printf("[OT] Relight started — N1=%.0f RPM\n", (double)ed.n1Rpm);
        }
        // Keep igniter on — checkRelight() clears this when flame returns or N1 drops
        ed.igniterOn = true;
    });

    // Web server on Core 0 — independent FreeRTOS task
    // Stack needs to hold char buf[5120] + ArduinoJson + call frames from webTask tick
    // Priority 8: high enough to time-share Core 0 with async_tcp (prio 10)
    // instead of being fully preempted during file serving. Keeps WS updates
    // regular even when the browser is fetching pages.
    xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 8, nullptr, 0);

    FlightRecorder::logBoot();

    if (HardwareConfig::hasClusterSerial)
        ClusterSerial::begin();   // sends boot table + initial status; before watchdog (uses delay())

    if (HardwareConfig::hasMAVLink && HardwareConfig::mavlinkTxPin >= 0) {
        _mavSerial.begin(HardwareConfig::mavlinkBaud, SERIAL_8N1,
                         -1, HardwareConfig::mavlinkTxPin);
        g_mavlink.begin(_mavSerial);
        Serial.printf("[OT] MAVLink TX on GPIO %d @ %d baud\n",
                      HardwareConfig::mavlinkTxPin, HardwareConfig::mavlinkBaud);
    }

    Watchdog::begin();

    Serial.println("[OT] Setup complete");
}

void loop() {
    Watchdog::feed();

    checkStopSwitch();
    checkStartSwitch();

    CommandQueue::drain(handleCommand);

    Hardware::updateSensors();

    // RC PWM input — updates rcIdle*/rcThrottle* and synthesises pot ADC values
    RCInput::tick();

    g_safety.check();

    g_sequencer.tick();
    g_abSequencer.tick();

    Hardware::runControllers();

    // Limp mode throttle cap (applied after controllers so it overrides DynamicIdle floor)
    {
        auto& ed = EngineData::instance();
        if (ed.limpMode && ed.mode == SysMode::RUNNING) {
            float cap = Config::limpMaxThrottlePct / 100.0f;
            if (ed.throttleDemand > cap) ed.throttleDemand = cap;
        }
    }

    checkToolTimers();
    checkExtraCooldown();
    checkRelight();
    checkABTrigger();
    checkStarterAssist();
    checkStandbyOilFeed();
    checkGeneralDI();
    buzzerTick();
    checkCooldownSkip();

    // Automation rules — run last in the control chain, after all normal writes
    // and safety checks, so rules can augment but not override safety shutdowns.
    RulesEngine::evaluate();

    Hardware::updateActuators();

    FlightRecorder::tick();
    SessionLogger::tick();

    if (HardwareConfig::hasClusterSerial)
        ClusterSerial::tick();

    if (HardwareConfig::hasMAVLink)
        g_mavlink.tick();

    Hardware::tickStatusLED();

    // Session peak tracking — health-gated so a failed sensor can't corrupt max values
    auto& edp = EngineData::instance();
    if (edp.n1Healthy        && edp.n1Rpm        > edp.maxN1)           edp.maxN1           = edp.n1Rpm;
    if (edp.n2Healthy        && edp.n2Rpm        > edp.maxN2)           edp.maxN2           = edp.n2Rpm;
    if (edp.totHealthy       && edp.tot          > edp.maxTot)          edp.maxTot          = edp.tot;
    if (edp.titHealthy       && edp.tit          > edp.maxTit)          edp.maxTit          = edp.tit;
    if (edp.fuelPressHealthy && edp.fuelPressure > edp.maxFuelPressure) edp.maxFuelPressure = edp.fuelPressure;
    // P1/P2/OilTemp/BattVoltage have no dedicated health flag — only update when sensor is enabled
    if (HardwareConfig::hasP1         && edp.p1          > edp.maxP1)          edp.maxP1          = edp.p1;
    if (HardwareConfig::hasP2         && edp.p2          > edp.maxP2)          edp.maxP2          = edp.p2;
    if (HardwareConfig::hasOilTemp    && edp.oilTempHealthy
                                       && edp.oilTemp    > edp.maxOilTemp)     edp.maxOilTemp     = edp.oilTemp;
    if (HardwareConfig::hasBattVoltage && edp.battVoltage > edp.maxBattVoltage) edp.maxBattVoltage = edp.battVoltage;

    edp.uptimeMs = millis();
}
