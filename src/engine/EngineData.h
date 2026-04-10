#pragma once
#include "Types.h"
#include <stdint.h>

// ── Afterburner state machine mode ────────────────────────────
enum class ABMode : uint8_t {
    Off         = 0,   // AB not active
    Arming      = 1,   // trigger detected, waiting for conditions
    Igniting    = 2,   // ignition sequence running
    Running     = 3,   // AB alight and stable
    ShuttingDown= 4,   // shutdown sequence running
    Fault       = 5,   // ignition failed or TOT exceeded
};

// ============================================================
//  EngineData — central volatile data bus
//
//  Written exclusively by the ECU loop (Core 1).
//  Read by web server (Core 0) without mutex — scalar volatile
//  reads on Xtensa are atomic enough for this use case.
//
//  Commands travel the other direction via CommandQueue only.
//  Nothing on Core 0 ever writes to this struct.
// ============================================================

struct EngineData {
    static EngineData& instance();

    // ── Sensor values ─────────────────────────────────────────
    volatile float    n1Rpm           = 0;      // RPM  (gas generator / main shaft)
    volatile float    n2Rpm           = 0;      // RPM  (power turbine / compressor 2)
    volatile float    tot             = 0;      // °C   turbine outlet temp
    volatile float    tit             = 0;      // °C   turbine inlet temp (optional)
    volatile float    oilPressure     = 0;      // bar  engine oil
    volatile float    oilTemp         = 0;      // °C   engine oil temperature (optional)
    volatile float    fuelPressure    = 0;      // bar  fuel rail pressure (optional)
    volatile int      fuelPressRaw   = 0;      // raw ADC counts for calibration
    volatile float    p1              = 0;      // bar  inlet pressure (optional)
    volatile float    p2              = 0;      // bar  exhaust pressure (optional)
    volatile float    fuelFlow        = 0;      // (optional, unit per cal)
    volatile float    battVoltage     = 0;      // V    battery / bus voltage (optional)
    volatile float    torque          = 0;      // Nm   output shaft torque (turboshaft, optional)
    volatile float    turboPower      = 0;      // W    shaft power = torque × n2AngularVel (turboshaft)
    volatile int      oilPressureRaw  = 0;      // raw ADC counts
    volatile int      flameSensorRaw  = 0;      // raw ADC counts
    volatile int      throttleInputRaw = 0;     // ADC counts (ADC mode) or equivalent synthetic (servo mode)
    volatile int      idleInputRaw     = 0;     // ADC counts (ADC mode) or equivalent synthetic (servo mode)

    // ── Sensor health ─────────────────────────────────────────
    volatile bool     n1Healthy       = false;
    volatile bool     n2Healthy       = true;    // unfitted → not a fault
    volatile bool     totHealthy      = false;
    volatile bool     titHealthy      = true;    // unfitted → not a fault
    volatile bool     oilHealthy      = false;
    volatile bool     oilTempHealthy  = true;    // unfitted → not a fault
    volatile bool     fuelPressHealthy= true;    // unfitted → not a fault
    volatile bool     battHealthy     = true;    // unfitted → not a fault
    volatile bool     torqueHealthy   = true;    // unfitted → not a fault
    volatile bool     flameDetected   = false;

    // ── Actuator demands (written by controllers/sequencer) ───
    volatile float    throttleDemand  = 0;      // 0.0–1.0  main fuel/throttle ESC
    volatile float    fuelPump2Demand = 0;      // 0.0–1.0  independent variable fuel pump
    volatile float    oilDemand       = 0;      // bar target → P-controller
    volatile float    starterDemand   = 0;      // 0.0–1.0
    volatile float    fuelPumpDemand  = 0;      // 0.0–1.0  AB pump / legacy fuel pump 2
    volatile float    propPitchDemand = 0;      // 0.0–1.0  propeller pitch servo (turboprop)
    volatile bool     fuelSolOpen     = false;
    volatile bool     igniterOn       = false;
    volatile bool     igniter2On      = false;
    volatile bool     starterEnabled  = false;
    volatile bool     coolFanOn       = false;  // cooling fan on/off
    volatile bool     airstarterOpen  = false;  // airstarter solenoid open
    volatile bool     oilScavengeOn   = false;  // separate scavenge pump on/off
    volatile bool     bleedValveOpen  = false;  // compressor bleed valve (surge prevention)

    // ── Safety / diagnostics ───────────────────────────────────
    volatile bool     surgeDetected   = false;  // compressor surge detected (N1 oscillation)
    volatile float    glowPlugPct     = 0;      // 0–100 % heat level for glow-plug ramp
    volatile float    glowCurrentAmps    = 0.0f;   // glow plug current (A), 0 if no sensor
    volatile bool     glowPlugHot        = false;   // true when current dropped below ready threshold
    volatile float    igniterCurrentAmps = 0.0f;   // igniter/coil current (A), 0 if no sensor

    // ── Afterburner state ─────────────────────────────────────
    volatile ABMode   abMode          = ABMode::Off;
    volatile bool     abTriggerActive = false;  // trigger input (throttle/switch/input) is asserted
    volatile bool     abArmSwitchOn   = false;  // arm switch currently asserted
    volatile bool     abFlameOn       = false;  // AB flame sensor detected (or TOT-rise confirmed)
    volatile bool     abSolOpen       = false;  // AB fuel solenoid (g_actAbSol)
    volatile int      abInputRaw      = 0;      // raw ADC/RC counts for analog/RC AB trigger

    // ── Engine state ──────────────────────────────────────────
    volatile SysMode  mode               = SysMode::STANDBY;
    volatile bool     flameMonitorActive = false;
    volatile bool     relightArmed       = false;
    volatile bool     devMode            = false;
    volatile bool     configLocked       = false;

    // ── Runtime flags (toggleable via web UI) ─────────────────
    volatile bool     skipSafetyChecks   = false;  // DEV_MODE only
    volatile bool     benchMode          = false;  // bench/debug: blocks complete on timer, safety bypassed
    volatile bool     dynamicIdleEnabled = true;
    volatile bool     limpMode           = false;
    volatile bool     stopSwitchActive   = false;
    volatile bool     startSwitchActive  = false;  // hardware start button currently pressed
    volatile bool     starterAssistActive = false; // starter assist enabled for this run
    volatile bool     manualRelightActive = false; // operator holding START while running

    // ── Last mode-change reason (best-effort display, no mutex) ──
    char              lastEvent[64]      = {};     // e.g. "Startup complete"

    // ── Plain-language fault / abort description ───────────────
    // Set by SafetyMonitor / enterAbortStandby before mode change.
    // Includes "what to do" guidance shown in the web UI fault banner.
    char              faultDescription[192] = {};

    // ── Sequence validation (written by Core 1 while in STANDBY) ─
    // validateSequences() walks the active block list and checks hardware
    // requirements. Results are read by Core 0 for telemetry / UI display.
    // Written only while mode == STANDBY so no mutex required.
    struct SeqIssue {
        char blockName[24];
        char reason[80];
        bool isError;   // true = blocks START, false = warning only
    };
    static constexpr int MAX_SEQ_ISSUES = 10;
    SeqIssue         seqIssues[MAX_SEQ_ISSUES] = {};
    uint8_t          seqIssueCount  = 0;
    bool             seqHasErrors   = false;

    // ── Sequence progress (written by SequenceEngine) ─────────
    char              currentBlock[32]   = {};     // name of active block, "" when idle
    volatile uint8_t  seqBlockIdx        = 0;      // 0-based index of current block
    volatile uint8_t  seqBlockTotal      = 0;      // total blocks in running sequence
    char              seqWaitReason[80]  = {};     // set by active block: "waiting for N1 > 42000 (currently 38500)"

    // ── Oil controller state (for web display) ────────────────
    volatile float    oilPctDemand       = 0;   // % duty currently driven
    volatile bool     oilFailsafeActive  = false;

    // ── Safety monitor: armed thresholds ─────────────────────
    // Set by sequencer as the startup progresses; 0 = check disabled
    volatile float    oilMinBar          = 0;   // 0 → not checking

    // ── Startup tracking ──────────────────────────────────────
    // True once FuelOpen fires this run.  CooldownSpin skips if never set
    // (aborted before ignition = no hot EGT to cool).  Cleared at STANDBY.
    volatile bool     fuelEverOpened     = false;

    // ── Relight tracking ──────────────────────────────────────
    // Counts relight attempts this run.  Reset on entering RUNNING or STANDBY.
    volatile uint8_t  relightAttempts    = 0;

    // ── Extra cooldown (standby only) ─────────────────────────
    // True while extra cooldown is running.  Cleared by checkExtraCooldown()
    // when TOT reaches totCooldownTarget or the timeout expires.
    volatile bool     extraCooldownActive = false;

    // ── General-purpose DI channel states ─────────────────────
    volatile bool diState[4] = {};   // current debounced active state of each DI channel

    // ── Config health (set by Config::load()) ─────────────────
    // True when the loaded ecu_config.json has a different config_version than
    // the firmware expects — new fields will have compile-time defaults.
    // Web UI should display a calibration reminder when this is set.
    volatile bool     configVersionMismatch = false;

    // ── Cluster display hint (optional plugin) ────────────────
    // Written by sequence blocks; read by ClusterSerial plugin.
    // 0 = nothing new to send.  Values match ClCode constants in ClusterSerial.h.
    volatile uint8_t  clusterCode        = 0;

    // ── RC PWM input (written by RCInput::tick(), hardware-profile opt-in) ──
    // Active only when OT_IDLE_INPUT_RC_PWM or OT_THROTTLE_INPUT_RC_PWM is defined.
    volatile bool     rcThrottleValid       = false;   // true = fresh PWM signal
    volatile float    rcThrottleNorm        = 0.0f;    // 0.0–1.0
    volatile bool     rcIdleValid           = false;
    volatile float    rcIdleNorm            = 0.0f;    // 0.0–1.0

    // ── Session peak values (reset at power-up, not persisted) ──
    // Thread-safety note: these are written only by Core 1 (ECU loop) and read
    // by Core 0 (web server) for display.  On Xtensa ESP32, S32I/L32I are
    // single-cycle 32-bit atomic operations, so torn reads of individual floats
    // cannot occur.  No mutex is needed here.
    volatile float    maxN1              = 0;
    volatile float    maxN2              = 0;
    volatile float    maxTot             = 0;
    volatile float    maxP1              = 0;
    volatile float    maxP2              = 0;
    volatile float    maxOilTemp         = 0;
    volatile float    maxBattVoltage     = 0;

    // ── EGT rate of rise (°C/s, calculated by SafetyMonitor) ──
    volatile float    totRiseRate        = 0.0f;   // positive = rising

    // ── Stats ─────────────────────────────────────────────────
    volatile uint32_t bootCount          = 0;
    volatile uint32_t runCount           = 0;   // entries into RUNNING
    volatile uint32_t uptimeMs           = 0;

private:
    EngineData() = default;
};
