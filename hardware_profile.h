#pragma once
// ============================================================
//  hardware_profile.h — OpenTurbine hardware topology
//
//  THIS IS THE ONLY FILE YOU EDIT FOR A NEW BUILD.
//  Define what hardware is physically present.
//  Pin assignments, sensor types, sequence blocks.
//  Comment / uncomment to enable/disable features.
//  Everything else adapts automatically at compile time.
// ============================================================

// ── Profile identity ─────────────────────────────────────────
// profile_id MUST match the "profile_id" field in config.json.
// A mismatch at boot locks out all engine operations.
#define OT_PROFILE_ID     "my_turbine_v1"
#define OT_PROFILE_DESC   "Example turbine build — edit me"

// ── Platform ─────────────────────────────────────────────────
#define OT_PLATFORM_ESP32          // ESP32 classic (240 MHz dual-core)
// #define OT_PLATFORM_ESP32S3     // future

// ── Development mode ─────────────────────────────────────────
// Uncomment to allow live config changes during engine operation,
// enable mock sensors/actuators, and bypass safety locks.
// NEVER ship firmware with this enabled.
// #define OT_DEV_MODE

// ── Mandatory physical controls ──────────────────────────────
#define OT_STOP_PIN    15    // active-low, internal pull-up — MUST be hardware
#define OT_START_PIN   13    // active-low, internal pull-up

// ── Sensors ──────────────────────────────────────────────────
// N1 shaft RPM via PCNT pulse counter (hardware, not interrupt)
#define OT_HAS_N1_RPM
#define OT_N1_RPM_PIN    14    // hall sensor signal
#define OT_N1_RPM_PPR    1.0f  // pulses per revolution

// EGT / TOT via MAX6675 thermocouple SPI
#define OT_HAS_TOT
#define OT_TOT_CLK       5
#define OT_TOT_CS        18
#define OT_TOT_MISO      19

// Oil pressure via analog (ADC1, polynomial calibrated)
#define OT_HAS_OIL_PRESS
#define OT_OIL_PRESS_PIN 34   // ADC1 channel only (36/39/34/35/32/33)

// Flame / ignition confirmation via analog threshold
#define OT_HAS_FLAME
#define OT_FLAME_PIN     35   // ADC1 channel only

// Optional — uncomment if fitted:
// N2 shaft RPM (e.g. compressor shaft on twin-spool)
// #define OT_HAS_N2_RPM
// #define OT_N2_RPM_PIN   27
// #define OT_N2_RPM_PPR   0.633f

// Fuel flow sensor (analog linear)
// #define OT_HAS_FUEL_FLOW
// #define OT_FUEL_FLOW_PIN 36

// Inlet pressure P1
// #define OT_HAS_P1
// #define OT_P1_PIN        36

// Exhaust pressure P2
// #define OT_HAS_P2
// #define OT_P2_PIN        39

// Throttle input — measures throttle stick/lever position (display + open-loop ref)
// Uncomment OT_HAS_THROTTLE_INPUT to enable. Choose signal type:
//   default = analog ADC voltage; OT_THROTTLE_INPUT_RC_PWM = servo PWM on same GPIO.
#define OT_HAS_THROTTLE_INPUT
#define OT_THROTTLE_INPUT_PIN    32
// #define OT_THROTTLE_INPUT_RC_PWM

// Idle input — sets idle RPM target range (potentiometer or RC channel)
#define OT_HAS_IDLE_INPUT
#define OT_IDLE_INPUT_PIN        33
// Input signal type — default = analog ADC voltage.
// Uncomment to use servo PWM on the same GPIO instead:
// #define OT_IDLE_INPUT_RC_PWM

// ── Actuators ─────────────────────────────────────────────────
// Throttle ESC (servo PWM)
// Signal range — standard unidirectional ESC: 1000–2000 µs
#define OT_HAS_THROTTLE
#define OT_THROTTLE_PIN          22
#define OT_THROTTLE_SERVO_MIN_US 1000  // armed / idle position
#define OT_THROTTLE_SERVO_MAX_US 2000  // full throttle

// Starter motor ESC (servo PWM)
// Signal range — choose to match your ESC type:
//   Standard unidirectional ESC : 1000–2000 µs  (armed=1000, full=2000)
//   Bidirectional ESC           : 1500–2000 µs  (neutral=1500, full forward=2000)
#define OT_HAS_STARTER
#define OT_STARTER_PIN           23
#define OT_STARTER_SERVO_MIN_US  1500  // bidirectional — change to 1000 for standard ESC
#define OT_STARTER_SERVO_MAX_US  2000

// Oil pump — GPIO pin is always required.
#define OT_HAS_OIL_PUMP
#define OT_OIL_PUMP_PIN       4

// Choose pump drive mode (uncomment ONE of the two blocks below, or leave both commented
// to use the default PWM mode):
//
//  MODE A — PWM BLDC (default, closed-loop capable via OT_HAS_OIL_LOOP)
//    10 kHz LEDC output, 12-bit duty, proportional pressure control.
#define OT_OIL_PUMP_FREQ_HZ   10000
#define OT_OIL_PUMP_RES_BITS  12
//
//  MODE B — On/Off relay or MOSFET (no P-loop, no OT_HAS_OIL_LOOP)
//    Pump is ON whenever oil is demanded (STARTUP/RUNNING/SHUTDOWN cooldown/windmill),
//    OFF in standby when engine is not spinning.
//    Comment out MODE A lines above and uncomment these two:
// #define OT_OIL_PUMP_ONOFF
// #define OT_OIL_PUMP_ONOFF_ACTIVE_H  true    // true = relay energised = pump ON

// Fuel solenoid (relay/MOSFET)
#define OT_HAS_FUEL_SOL
#define OT_FUEL_SOL_PIN       12
#define OT_FUEL_SOL_ACTIVE_H  true

// Igniter — relay / MOSFET (default) or direct coil drive (PWM)
#define OT_HAS_IGNITER
#define OT_IGNITER_PIN        21
#define OT_IGNITER_ACTIVE_H   true   // used in relay mode only

// ── Direct coil drive (uncomment OT_IGNITER_PWM for inductive coil igniter) ──
// Generates dwell/rest cycle: coil charges for DWELL_MS, sparks during REST_MS.
// Frequency = 1000 / (DWELL_MS + REST_MS), duty = DWELL_MS / (DWELL_MS + REST_MS)
// Do NOT define OT_IGNITER_FREQ_HZ — it is computed automatically.
// #define OT_IGNITER_PWM
// #define OT_IGNITER_DWELL_MS  6    // coil charge time per cycle (default 6 ms)
// #define OT_IGNITER_REST_MS   3    // spark / discharge time per cycle (default 3 ms)

// Starter enable relay (powers ESC)
#define OT_HAS_STARTER_EN
#define OT_STARTER_EN_PIN     25
#define OT_STARTER_EN_ACTIVE_H true

// Optional actuators:
// Afterburner fuel solenoid
// #define OT_HAS_AB_SOL
// #define OT_AB_SOL_PIN       XX
// #define OT_AB_SOL_ACTIVE_H  true

// Air-starter solenoid
// #define OT_HAS_AIRSTARTER_SOL
// #define OT_AIRSTARTER_SOL_PIN XX

// Cooling fan relay
// #define OT_HAS_COOL_FAN
// #define OT_COOL_FAN_PIN     XX

// ── Controllers ──────────────────────────────────────────────
// All require matching hardware above to be defined.
#define OT_HAS_OIL_LOOP           // P-controller: OIL_PRESS → OIL_PUMP
#define OT_HAS_THROTTLE_SLEW      // Rate-limiter on throttle output
#define OT_HAS_DYNAMIC_IDLE       // Closed-loop idle RPM hold

// Use N2 as idle control source instead of N1 (requires OT_HAS_N2_RPM):
// #define OT_DYNAMIC_IDLE_USE_N2

// ── Safety sources ───────────────────────────────────────────
// Each requires the corresponding sensor to be defined above.
#define OT_SAFETY_OVERSPEED       // N1 > RPM_LIMIT → immediate shutdown
#define OT_SAFETY_OVERTEMP        // TOT > TOT_LIMIT → shutdown
#define OT_SAFETY_LOW_OIL         // oil < min bar → shutdown
#define OT_SAFETY_OIL_ZERO        // oil near-zero during RUNNING → catastrophic loss fault
#define OT_SAFETY_FLAMEOUT        // flame lost sustained → shutdown

// Optional:
// #define OT_SAFETY_LOW_FUEL     // requires OT_HAS_FUEL_FLOW

// ── Startup sequence ─────────────────────────────────────────
// Order matters. Comment out to remove a block.
#define OT_STARTUP_SEQ \
    OT_BLOCK(OilPrime)       \
    OT_BLOCK(StarterSpin)    \
    OT_BLOCK(PreIgnSpark)    \
    OT_BLOCK(FuelOpen)       \
    OT_BLOCK(FlameConfirm)   \
    OT_BLOCK(PostIgnDwell)   \
    OT_BLOCK(Spool)          \
    OT_BLOCK(SafetyHold)

// ── Shutdown sequence ─────────────────────────────────────────
#define OT_SHUTDOWN_SEQ \
    OT_BLOCK(ImmediateCut)   \
    OT_BLOCK(RPMDrop)        \
    OT_BLOCK(CooldownSpin)   \
    OT_BLOCK(FinalStop)

// ── Optional feature blocks ──────────────────────────────────
// #define OT_HAS_AFTERBURNER
// #define OT_HAS_AIRSTARTER

// ── Status LED (mode blink indicator) ────────────────────────
// Blink pattern: STANDBY=1, STARTUP=2, RUNNING=3, SHUTDOWN=4, FAULT=rapid
// Built-in LED is usually GPIO 2 on ESP32 dev boards.
// #define OT_HAS_STATUS_LED
// #define OT_STATUS_LED_PIN  2

// ── Instrument cluster serial output (GPX750-compatible) ─────
// Outputs JetEcu cluster protocol on a dedicated UART TX pin.
// Configure TX pin to a free GPIO (avoid GPIO1/3 = USB serial).
// Baud: 115200 (must match cluster firmware). Interval: data packet rate.
// #define OT_HAS_CLUSTER_SERIAL
// #define OT_CLUSTER_TX_PIN        17     // free GPIO, UART2 TX
// #define OT_CLUSTER_BAUD          115200
// #define OT_CLUSTER_INTERVAL_MS   50     // sensor data packet rate (ms)

// ── Compile-time sanity checks ───────────────────────────────
#ifndef OT_PROFILE_ID
  #error "OT_PROFILE_ID must be defined in hardware_profile.h"
#endif
#ifndef OT_STOP_PIN
  #error "OT_STOP_PIN must be defined in hardware_profile.h"
#endif
#if defined(OT_SAFETY_OVERSPEED) && !defined(OT_HAS_N1_RPM)
  #error "OT_SAFETY_OVERSPEED requires OT_HAS_N1_RPM"
#endif
#if defined(OT_SAFETY_OVERTEMP) && !defined(OT_HAS_TOT)
  #error "OT_SAFETY_OVERTEMP requires OT_HAS_TOT"
#endif
#if defined(OT_SAFETY_LOW_OIL) && !defined(OT_HAS_OIL_PRESS)
  #error "OT_SAFETY_LOW_OIL requires OT_HAS_OIL_PRESS"
#endif
#if defined(OT_SAFETY_FLAMEOUT) && !defined(OT_HAS_FLAME)
  #error "OT_SAFETY_FLAMEOUT requires OT_HAS_FLAME"
#endif
#if defined(OT_HAS_OIL_LOOP) && !(defined(OT_HAS_OIL_PRESS) && defined(OT_HAS_OIL_PUMP))
  #error "OT_HAS_OIL_LOOP requires OT_HAS_OIL_PRESS and OT_HAS_OIL_PUMP"
#endif
#if defined(OT_OIL_PUMP_ONOFF) && !defined(OT_HAS_OIL_PUMP)
  #error "OT_OIL_PUMP_ONOFF requires OT_HAS_OIL_PUMP"
#endif
#if defined(OT_OIL_PUMP_ONOFF) && defined(OT_HAS_OIL_LOOP)
  #error "OT_OIL_PUMP_ONOFF is incompatible with OT_HAS_OIL_LOOP — on/off pump cannot be P-controlled; remove OT_HAS_OIL_LOOP when using ONOFF mode"
#endif
#if defined(OT_HAS_DYNAMIC_IDLE) && !defined(OT_HAS_THROTTLE)
  #error "OT_HAS_DYNAMIC_IDLE requires OT_HAS_THROTTLE"
#endif
#if defined(OT_DYNAMIC_IDLE_USE_N2) && !defined(OT_HAS_N2_RPM)
  #error "OT_DYNAMIC_IDLE_USE_N2 requires OT_HAS_N2_RPM"
#endif
#if defined(OT_IDLE_INPUT_RC_PWM) && !defined(OT_HAS_IDLE_INPUT)
  #error "OT_IDLE_INPUT_RC_PWM requires OT_HAS_IDLE_INPUT"
#endif
#if defined(OT_THROTTLE_INPUT_RC_PWM) && !defined(OT_HAS_THROTTLE_INPUT)
  #error "OT_THROTTLE_INPUT_RC_PWM requires OT_HAS_THROTTLE_INPUT"
#endif
