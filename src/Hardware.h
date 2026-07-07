#pragma once
// ============================================================
//  Hardware.h — runtime hardware dispatch
//
//  Reads HardwareConfig (loaded from the hardware section of /ecu_config.json) at boot.
//  All sensor/actuator objects are always compiled in; feature
//  enable/disable is controlled by HardwareConfig runtime flags
//  instead of compile-time #ifdef OT_HAS_* guards.
//
//  hardware_profile.h is still included for compile-time defaults
//  used by HardwareConfig::applyDefaults() and boot-control defines
//  such as OT_STOP_PIN and OT_START_PIN.
//
//  Pin assignments pass through Hardware::initSensors/initActuators
//  at boot time; changing the hardware section and rebooting applies new pins.
//
//  main.cpp includes this once and calls Hardware::init* / update*.
// ============================================================

#include "../hardware_profile.h"
#include "system/HardwareConfig.h"

// ── All sensor headers — always included ──────────────────────
#include "hal/sensors/PCNTRpmSensor.h"
#include "hal/sensors/MAX6675TempSensor.h"
#include "hal/sensors/MAX31855TempSensor.h"
#include "hal/sensors/MAX31856TempSensor.h"
#include "hal/sensors/AnalogSensor.h"
#include "hal/sensors/NTCSensor.h"
#include "hal/sensors/DS18B20TempSensor.h"
#include "hal/sensors/HX711Sensor.h"

// ── All actuator headers — always included ────────────────────
#include "hal/actuators/ServoActuator.h"
#include "hal/actuators/LEDCActuator.h"
#include "hal/actuators/RelayActuator.h"
#include "hal/actuators/IActuator.h"

// ── Controller headers — always included ──────────────────────
#include "engine/controllers/OilPressureLoop.h"
#include "engine/controllers/ThrottleSlew.h"
#include "engine/controllers/DynamicIdle.h"
#include "engine/controllers/PowerTurbineGovernor.h"

// ── Sequence block headers ────────────────────────────────────
#include "engine/sequencer/blocks/AdvancedBlocks.h"
#include "engine/sequencer/blocks/OilPrime.h"
#include "engine/sequencer/blocks/StarterSpin.h"
#include "engine/sequencer/blocks/PreIgnSpark.h"
#include "engine/sequencer/blocks/FuelOpen.h"
#include "engine/sequencer/blocks/FlameConfirm.h"
#include "engine/sequencer/blocks/TempConfirm.h"
#include "engine/sequencer/blocks/TimedDelay.h"
#include "engine/sequencer/blocks/FuelPumpIdle.h"
#include "engine/sequencer/blocks/ModifiedIdle.h"
#include "engine/sequencer/blocks/Spool.h"
#include "engine/sequencer/blocks/SafetyHold.h"
#include "engine/sequencer/blocks/ImmediateCut.h"
#include "engine/sequencer/blocks/RPMDrop.h"
#include "engine/sequencer/blocks/CooldownSpin.h"
#include "engine/sequencer/blocks/FinalStop.h"
#include "engine/sequencer/blocks/ActuatorBlocks.h"
#include "engine/sequencer/blocks/MoreBlocks.h"
#include "engine/sequencer/blocks/WaitForInput.h"
#include "engine/sequencer/blocks/ABCheckReady.h"
#include "engine/sequencer/blocks/ABIgnite.h"
#include "engine/sequencer/blocks/ABFlameConfirm.h"
#include "engine/sequencer/blocks/ABStabilize.h"
#include "engine/sequencer/SequenceEngine.h"
#include "engine/SafetyMonitor.h"
#include "system/Config.h"
#include "platform/esp32/StatusLED.h"

// ============================================================
//  OT_DECLARE_HARDWARE — put in global scope in main.cpp.
//  All sensor/actuator/controller/block objects always declared.
//  For oil pump and igniter, both relay and LEDC variants are
//  declared; g_actOilPump / g_actIgniter are IActuator* pointers
//  set to the active one by initActuators().
// ============================================================

// Servo signal range defaults (used when the engine file has no override)
#ifndef OT_THROTTLE_SERVO_MIN_US
  #define OT_THROTTLE_SERVO_MIN_US 1000
#endif
#ifndef OT_THROTTLE_SERVO_MAX_US
  #define OT_THROTTLE_SERVO_MAX_US 2000
#endif
#ifndef OT_STARTER_SERVO_MIN_US
  #define OT_STARTER_SERVO_MIN_US  1000
#endif
#ifndef OT_STARTER_SERVO_MAX_US
  #define OT_STARTER_SERVO_MAX_US  2000
#endif
#ifndef OT_OIL_PUMP_ONOFF_ACTIVE_H
  #define OT_OIL_PUMP_ONOFF_ACTIVE_H true
#endif
#ifndef OT_IGNITER_DWELL_MS
  #define OT_IGNITER_DWELL_MS 6
#endif
#ifndef OT_IGNITER_REST_MS
  #define OT_IGNITER_REST_MS  3
#endif

#define OT_DECLARE_HARDWARE \
    /* ── Sensors ──────────────────────────────────────────────────────────── */ \
    PCNTRpmSensor      g_sensorN1Rpm(OT_N1_RPM_PIN, OT_N1_RPM_PPR, "N1_RPM");   \
    PCNTRpmSensor      g_sensorN2Rpm(27, 0.633f, "N2_RPM");                      \
    MAX6675TempSensor    g_sensorTot(OT_TOT_CLK, OT_TOT_CS, OT_TOT_MISO, "TOT"); \
    MAX31855TempSensor   g_sensorTotAlt(OT_TOT_CLK, OT_TOT_CS, OT_TOT_MISO, "TOT_ALT"); \
    MAX31856TempSensor   g_sensorTot31856(OT_TOT_CLK, OT_TOT_CS, OT_TOT_MISO, -1, "K", "TOT_31856"); \
    ISensor*             g_pSensorTot = nullptr;                                  \
    MAX6675TempSensor    g_sensorTit(-1, -1, -1, "TIT");                         \
    MAX31855TempSensor   g_sensorTitAlt(-1, -1, -1, "TIT_ALT");                  \
    MAX31856TempSensor   g_sensorTit31856(-1, -1, -1, -1, "K", "TIT_31856");     \
    ISensor*             g_pSensorTit = nullptr;                                  \
    /* Oil temp sensor (NTC analog, SPI thermocouple, or DS18B20 OneWire) */       \
    MAX6675TempSensor    g_sensorOilTempTc(-1, -1, -1, "OIL_TEMP_TC");           \
    MAX31855TempSensor   g_sensorOilTemp855(-1, -1, -1, "OIL_TEMP_855");         \
    MAX31856TempSensor   g_sensorOilTemp856(-1, -1, -1, -1, "K", "OIL_TEMP_856");\
    NTCSensor            g_sensorOilTempNtc(-1, "OIL_TEMP_NTC");                 \
    DS18B20TempSensor    g_sensorOilTempDs18b20("OIL_TEMP_DS18B20");             \
    ISensor*             g_pSensorOilTemp = nullptr;                              \
    /* Battery voltage and torque sensors */                                      \
    AnalogLinearSensor   g_sensorBattVolt(-1, "BATT_VOLT");                      \
    AnalogLinearSensor   g_sensorTorque(-1, "TORQUE");                           \
    HX711Sensor           g_sensorTorqueHx711(-1, -1, "TORQUE_HX711");            \
    AnalogPolySensor   g_sensorOilPress(OT_OIL_PRESS_PIN, "OIL_PRESS");          \
    AnalogThSensor     g_sensorFlame(OT_FLAME_PIN, "FLAME");                     \
    AnalogLinearSensor g_sensorIdleInput(OT_IDLE_INPUT_PIN, "IDLE_INPUT");       \
    AnalogLinearSensor g_sensorThrottleInput(OT_THROTTLE_INPUT_PIN, "THROTTLE_INPUT"); \
    AnalogLinearSensor g_sensorFuelFlow(36, "FUEL_FLOW");                        \
    PCNTRpmSensor      g_sensorFuelFlowPulse(-1, 1.0f, "FUEL_FLOW_PULSE");      \
    AnalogLinearSensor g_sensorFuelPress(36, "FUEL_PRESS");                      \
    AnalogLinearSensor g_sensorP1(36, "P1");                                     \
    AnalogLinearSensor g_sensorP2(39, "P2");                                     \
    AnalogThSensor     g_sensorAbFlame(-1, "AB_FLAME");                          \
    AnalogLinearSensor g_sensorAbInput(-1, "AB_INPUT");                          \
    AnalogLinearSensor g_sensorGlowCurrent(-1, "GLOW_CURRENT");                  \
    AnalogLinearSensor g_sensorIgniterCurrent(-1, "IGNITER_CURRENT");             \
    AnalogLinearSensor g_sensorIgniter2Current(-1, "IGNITER2_CURRENT");           \
    AnalogLinearSensor g_sensorOilPumpCurrent(-1, "OIL_PUMP_CURRENT");           \
    /* ── Actuators ─────────────────────────────────────────────────────────── */ \
    /* Throttle: servo / LEDC-PWM / on-off — pointer set by initActuators() */   \
    ServoActuator  g_actThrottleServo(OT_THROTTLE_PIN, OT_THROTTLE_SERVO_MIN_US, OT_THROTTLE_SERVO_MAX_US, "THROTTLE_SRV"); \
    LEDCActuator   g_actThrottleLedc(OT_THROTTLE_PIN, 10000, 12, "THROTTLE_LEDC"); \
    RelayActuator  g_actThrottleOnOff(OT_THROTTLE_PIN, true, "THROTTLE_ONOFF");   \
    IActuator*     g_actThrottle = nullptr;                                        \
    /* Starter: servo / LEDC-PWM / on-off */                                       \
    ServoActuator  g_actStarterServo(OT_STARTER_MOTOR_PIN, OT_STARTER_SERVO_MIN_US, OT_STARTER_SERVO_MAX_US, "STARTER_SRV"); \
    LEDCActuator   g_actStarterLedc(OT_STARTER_MOTOR_PIN, 10000, 12, "STARTER_LEDC"); \
    RelayActuator  g_actStarterOnOff(OT_STARTER_MOTOR_PIN, true, "STARTER_ONOFF"); \
    IActuator*     g_actStarter = nullptr;                                         \
    /* Oil pump: servo / LEDC-PWM / on-off */                                      \
    ServoActuator  g_actOilPumpServo(OT_OIL_PUMP_PIN, 1000, 2000, "OIL_PUMP_SRV"); \
    LEDCActuator   g_actOilPumpLedc(OT_OIL_PUMP_PIN, OT_OIL_PUMP_FREQ_HZ, OT_OIL_PUMP_RES_BITS, "OIL_PUMP"); \
    RelayActuator  g_actOilPumpRelay(OT_OIL_PUMP_PIN, OT_OIL_PUMP_ONOFF_ACTIVE_H, "OIL_PUMP_RELAY"); \
    IActuator*     g_actOilPump = nullptr;                                        \
    /* Scavenge pump: servo / LEDC-PWM / on-off */                                     \
    ServoActuator  g_actOilScavServo(-1, 1000, 2000, "OIL_SCAV_SRV");                 \
    LEDCActuator   g_actOilScavLedc(-1, 10000, 12, "OIL_SCAV_LEDC");                 \
    RelayActuator  g_actOilScavRelay(-1, true, "OIL_SCAV");                           \
    IActuator*     g_actOilScavPump = nullptr;                                         \
    RelayActuator  g_actFuelSol(OT_FUEL_SOL_PIN, OT_FUEL_SOL_ACTIVE_H, "FUEL_SOL"); \
    LEDCActuator   g_actIgniterLedc(OT_IGNITER_PIN, 1000/(OT_IGNITER_DWELL_MS+OT_IGNITER_REST_MS), 8, "IGNITER_LEDC"); \
    RelayActuator  g_actIgniterRelay(OT_IGNITER_PIN, OT_IGNITER_ACTIVE_H, "IGNITER_RELAY"); \
    IActuator*     g_actIgniter = nullptr;                                        \
    /* Igniter 2 */                                                                \
    LEDCActuator   g_actIgniter2Ledc(-1, 111, 8, "IGNITER2_LEDC");               \
    RelayActuator  g_actIgniter2Relay(-1, true, "IGNITER2_RELAY");                \
    IActuator*     g_actIgniter2 = nullptr;                                       \
    RelayActuator  g_actStarterEn(OT_STARTER_EN_PIN, OT_STARTER_EN_ACTIVE_H, "STARTER_EN"); \
    RelayActuator  g_actAbSol(-1, true, "AB_SOL");                               \
    RelayActuator  g_actAirstarterSol(-1, true, "AIRSTARTER_SOL");               \
    /* Cool fan: servo / LEDC-PWM / on-off */                                      \
    ServoActuator  g_actCoolFanServo(-1, 1000, 2000, "COOL_FAN_SRV");            \
    LEDCActuator   g_actCoolFanLedc(-1, 10000, 12, "COOL_FAN_LEDC");             \
    RelayActuator  g_actCoolFan(-1, true, "COOL_FAN");                           \
    IActuator*     g_pActCoolFan = nullptr;                                       \
    /* Afterburner pump: servo / LEDC-PWM / on-off */                             \
    ServoActuator  g_actAbPumpServo(-1, 1000, 2000, "AB_PUMP_SRV");              \
    LEDCActuator   g_actAbPumpLedc(-1, 10000, 12, "AB_PUMP_LEDC");               \
    RelayActuator  g_actAbPumpRelay(-1, true, "AB_PUMP");                         \
    IActuator*     g_actAbPump = nullptr;                                         \
    /* Fuel pump 2: servo / LEDC-PWM / on-off */                                  \
    ServoActuator  g_actFuelPump2Servo(-1, 1000, 2000, "FUEL_PUMP2_SRV");        \
    LEDCActuator   g_actFuelPump2Ledc(-1, 10000, 12, "FUEL_PUMP2");             \
    RelayActuator  g_actFuelPump2Relay(-1, true, "FUEL_PUMP2_RELAY");            \
    IActuator*     g_actFuelPump2 = nullptr;                                     \
    /* Compressor bleed valve: on-off / servo / ledc-pwm */                      \
    RelayActuator  g_actBleedValveRelay(-1, true, "BLEED_VALVE");                \
    ServoActuator  g_actBleedValveServo(-1, 1000, 2000, "BLEED_VALVE_SRV");     \
    LEDCActuator   g_actBleedValveLedc(-1, 1000, 10, "BLEED_VALVE_LEDC");       \
    IActuator*     g_actBleedValve = nullptr;                                    \
    /* Propeller pitch actuator: servo / ledc-pwm / on-off */                   \
    ServoActuator  g_actPropPitchServo(-1, 1000, 2000, "PROP_PITCH_SRV");       \
    LEDCActuator   g_actPropPitchLedc(-1, 1000, 10, "PROP_PITCH_LEDC");         \
    RelayActuator  g_actPropPitchRelay(-1, true, "PROP_PITCH");                  \
    IActuator*     g_actPropPitch = nullptr;                                     \
    LEDCActuator   g_actGlowPlug(-1, 1000, 8, "GLOW_PLUG");                     \
    RelayActuator  g_actGlowPlugRelay(-1, true, "GLOW_PLUG_RELAY");             \
    RelayActuator  g_actWetGlowFuelRelay(-1, true, "WET_GLOW_FUEL");            \
    LEDCActuator   g_actWetGlowFuelLedc(-1, 1000, 10, "WET_GLOW_FUEL_PWM");     \
    ServoActuator  g_actWetGlowFuelServo(-1, 1000, 2000, "WET_GLOW_FUEL_SRV");  \
    IActuator*     g_actWetGlowFuel = nullptr;                                  \
    /* ── Controllers ───────────────────────────────────────────────────────── */ \
    OilPressureLoop       g_ctrlOilLoop;                                          \
    ThrottleSlew          g_ctrlThrottleSlew;                                     \
    DynamicIdle           g_ctrlDynamicIdle;                                      \
    PowerTurbineGovernor  g_ctrlGovernor;                                         \
    /* ── Sequence blocks ───────────────────────────────────────────────────── */ \
    OilPrime     g_blkOilPrime;                                                   \
    StarterSpin  g_blkStarterSpin;                                                \
    PreIgnSpark  g_blkPreIgnSpark;                                                \
    FuelOpen     g_blkFuelOpen;                                                   \
    FlameConfirm g_blkFlameConfirm;                                               \
    TempConfirm  g_blkTempConfirm;                                                \
    TimedDelay   g_blkTimedDelay;                                                 \
    FuelPumpIdle g_blkFuelPumpIdle;                                               \
    ModifiedIdle g_blkModifiedIdle;                                               \
    Spool        g_blkSpool;                                                      \
    SafetyHold   g_blkSafetyHold;                                                 \
    ImmediateCut g_blkImmediateCut;                                               \
    RPMDrop      g_blkRPMDrop;                                                    \
    CooldownSpin g_blkCooldownSpin;                                               \
    FinalStop    g_blkFinalStop;                                                   \
    /* ── Simple actuator blocks ────────────────────────────────────────────── */ \
    IgniterOn    g_blkIgniterOn;    IgniterOff   g_blkIgniterOff;                \
    FuelSolClose g_blkFuelSolClose;                                               \
    StarterEnOn  g_blkStarterEnOn;  StarterEnOff g_blkStarterEnOff;             \
    StarterOff   g_blkStarterOff;                                                 \
    OilPumpOn    g_blkOilPumpOn;    OilPumpOff   g_blkOilPumpOff;               \
    CoolFanOn    g_blkCoolFanOn;    CoolFanOff   g_blkCoolFanOff;               \
    AirstarterOn g_blkAirstarterOn; AirstarterOff g_blkAirstarterOff;           \
    ABPumpOn     g_blkABPumpOn;     ABPumpOff    g_blkABPumpOff;                \
    ABIgnOn      g_blkABIgnOn;      ABIgnOff     g_blkABIgnOff;                 \
    ABSolOpen    g_blkABSolOpen;    ABSolClose   g_blkABSolClose;               \
    OilScavengeOn  g_blkOilScavengeOn;  OilScavengeOff g_blkOilScavengeOff;          \
    /* ── MoreBlocks ─────────────────────────────────────────────────────────── */ \
    FuelPulse    g_blkFuelPulse;                                                  \
    WaitTOTCool  g_blkWaitTOTCool;                                                \
    WaitForInput g_blkWaitForInput; WaitForInputOff g_blkWaitForInputOff;         \
    ThrottleSet  g_blkThrottleSet;                                                \
    PreHeat      g_blkPreHeat;                                                    \
    /* ── Advanced sequence blocks ───────────────────────────────────────────── */ \
    GlowPreheat  g_blkGlowPreheat;                                                \
    BleedOpen    g_blkBleedOpen;                                                  \
    BleedClose   g_blkBleedClose;                                                 \
    FuelPumpRamp g_blkFuelPumpRamp;                                               \
    GovernorHold g_blkGovernorHold;                                               \
    FuelPump2Set g_blkFuelPump2Set;                                               \
    FuelPump2On  g_blkFuelPump2On;                                                \
    FuelPump2Off g_blkFuelPump2Off;                                               \
    /* ── AB sequence blocks ─────────────────────────────────────────────────── */ \
    ABCheckReady  g_blkABCheckReady;                                              \
    ABIgnite      g_blkABIgnite;                                                  \
    ABFlameConfirm g_blkABFlameConfirm;                                           \
    ABStabilize   g_blkABStabilize;                                               \
    SequenceEngine g_sequencer;                                                   \
    SequenceEngine g_abSequencer;                                                 \
    SafetyMonitor  g_safety;

// ============================================================
//  Forward declarations (used by Hardware:: inline functions)
// ============================================================

extern PCNTRpmSensor      g_sensorN1Rpm;
extern PCNTRpmSensor      g_sensorN2Rpm;
extern MAX6675TempSensor   g_sensorTot;
extern MAX31855TempSensor  g_sensorTotAlt;
extern MAX31856TempSensor  g_sensorTot31856;
extern ISensor*            g_pSensorTot;
extern MAX6675TempSensor   g_sensorTit;
extern MAX31855TempSensor  g_sensorTitAlt;
extern MAX31856TempSensor  g_sensorTit31856;
extern ISensor*            g_pSensorTit;
extern MAX6675TempSensor   g_sensorOilTempTc;
extern MAX31855TempSensor  g_sensorOilTemp855;
extern MAX31856TempSensor  g_sensorOilTemp856;
extern NTCSensor           g_sensorOilTempNtc;
extern DS18B20TempSensor   g_sensorOilTempDs18b20;
extern ISensor*            g_pSensorOilTemp;
extern AnalogLinearSensor  g_sensorBattVolt;
extern AnalogLinearSensor  g_sensorTorque;
extern HX711Sensor          g_sensorTorqueHx711;
extern AnalogPolySensor   g_sensorOilPress;
extern AnalogThSensor     g_sensorFlame;
extern AnalogLinearSensor g_sensorIdleInput;
extern AnalogLinearSensor g_sensorThrottleInput;
extern AnalogLinearSensor g_sensorFuelFlow;
extern PCNTRpmSensor      g_sensorFuelFlowPulse;
extern AnalogLinearSensor g_sensorFuelPress;
extern AnalogLinearSensor g_sensorP1;
extern AnalogLinearSensor g_sensorP2;
extern AnalogThSensor     g_sensorAbFlame;
extern AnalogLinearSensor g_sensorAbInput;
extern AnalogLinearSensor g_sensorGlowCurrent;
extern AnalogLinearSensor g_sensorIgniterCurrent;
extern AnalogLinearSensor g_sensorIgniter2Current;
extern AnalogLinearSensor g_sensorOilPumpCurrent;

extern ServoActuator  g_actThrottleServo;
extern LEDCActuator   g_actThrottleLedc;
extern RelayActuator  g_actThrottleOnOff;
extern IActuator*     g_actThrottle;
extern ServoActuator  g_actStarterServo;
extern LEDCActuator   g_actStarterLedc;
extern RelayActuator  g_actStarterOnOff;
extern IActuator*     g_actStarter;
extern ServoActuator  g_actOilPumpServo;
extern LEDCActuator   g_actOilPumpLedc;
extern RelayActuator  g_actOilPumpRelay;
extern IActuator*     g_actOilPump;
extern ServoActuator  g_actOilScavServo;
extern LEDCActuator   g_actOilScavLedc;
extern RelayActuator  g_actOilScavRelay;
extern IActuator*     g_actOilScavPump;
extern RelayActuator  g_actFuelSol;
extern LEDCActuator   g_actIgniterLedc;
extern RelayActuator  g_actIgniterRelay;
extern IActuator*     g_actIgniter;
extern LEDCActuator   g_actIgniter2Ledc;
extern RelayActuator  g_actIgniter2Relay;
extern IActuator*     g_actIgniter2;
extern RelayActuator  g_actStarterEn;
extern RelayActuator  g_actAbSol;
extern RelayActuator  g_actAirstarterSol;
extern ServoActuator  g_actCoolFanServo;
extern LEDCActuator   g_actCoolFanLedc;
extern RelayActuator  g_actCoolFan;
extern IActuator*     g_pActCoolFan;
extern ServoActuator  g_actAbPumpServo;
extern LEDCActuator   g_actAbPumpLedc;
extern RelayActuator  g_actAbPumpRelay;
extern IActuator*     g_actAbPump;
extern ServoActuator  g_actFuelPump2Servo;
extern LEDCActuator   g_actFuelPump2Ledc;
extern RelayActuator  g_actFuelPump2Relay;
extern IActuator*     g_actFuelPump2;
extern RelayActuator  g_actBleedValveRelay;
extern ServoActuator  g_actBleedValveServo;
extern LEDCActuator   g_actBleedValveLedc;
extern IActuator*     g_actBleedValve;
extern ServoActuator  g_actPropPitchServo;
extern LEDCActuator   g_actPropPitchLedc;
extern RelayActuator  g_actPropPitchRelay;
extern IActuator*     g_actPropPitch;
extern LEDCActuator   g_actGlowPlug;
extern RelayActuator  g_actGlowPlugRelay;
extern RelayActuator  g_actWetGlowFuelRelay;
extern LEDCActuator   g_actWetGlowFuelLedc;
extern ServoActuator  g_actWetGlowFuelServo;
extern IActuator*     g_actWetGlowFuel;

extern OilPressureLoop      g_ctrlOilLoop;
extern ThrottleSlew         g_ctrlThrottleSlew;
extern DynamicIdle          g_ctrlDynamicIdle;
extern PowerTurbineGovernor g_ctrlGovernor;

extern OilPrime      g_blkOilPrime;
extern StarterSpin   g_blkStarterSpin;
extern PreIgnSpark   g_blkPreIgnSpark;
extern FuelOpen      g_blkFuelOpen;
extern FlameConfirm  g_blkFlameConfirm;
extern TempConfirm   g_blkTempConfirm;
extern TimedDelay    g_blkTimedDelay;
extern FuelPumpIdle  g_blkFuelPumpIdle;
extern ModifiedIdle  g_blkModifiedIdle;
extern Spool         g_blkSpool;
extern SafetyHold    g_blkSafetyHold;
extern ImmediateCut  g_blkImmediateCut;
extern RPMDrop       g_blkRPMDrop;
extern CooldownSpin  g_blkCooldownSpin;
extern FinalStop     g_blkFinalStop;
extern IgniterOn     g_blkIgniterOn;    extern IgniterOff    g_blkIgniterOff;
extern FuelSolClose  g_blkFuelSolClose;
extern StarterEnOn   g_blkStarterEnOn;  extern StarterEnOff  g_blkStarterEnOff;
extern StarterOff    g_blkStarterOff;
extern OilPumpOn     g_blkOilPumpOn;    extern OilPumpOff    g_blkOilPumpOff;
extern CoolFanOn     g_blkCoolFanOn;    extern CoolFanOff    g_blkCoolFanOff;
extern AirstarterOn  g_blkAirstarterOn; extern AirstarterOff g_blkAirstarterOff;
extern ABPumpOn      g_blkABPumpOn;     extern ABPumpOff     g_blkABPumpOff;
extern ABIgnOn       g_blkABIgnOn;      extern ABIgnOff      g_blkABIgnOff;
extern ABSolOpen     g_blkABSolOpen;    extern ABSolClose    g_blkABSolClose;
extern OilScavengeOn  g_blkOilScavengeOn;
extern OilScavengeOff g_blkOilScavengeOff;
extern FuelPulse     g_blkFuelPulse;
extern WaitTOTCool   g_blkWaitTOTCool;
extern WaitForInput  g_blkWaitForInput;
extern WaitForInputOff g_blkWaitForInputOff;
extern ThrottleSet   g_blkThrottleSet;
extern PreHeat       g_blkPreHeat;
extern GlowPreheat   g_blkGlowPreheat;
extern BleedOpen     g_blkBleedOpen;
extern BleedClose    g_blkBleedClose;
extern FuelPumpRamp  g_blkFuelPumpRamp;
extern GovernorHold  g_blkGovernorHold;
extern FuelPump2Set  g_blkFuelPump2Set;
extern FuelPump2On   g_blkFuelPump2On;
extern FuelPump2Off  g_blkFuelPump2Off;
extern ABCheckReady  g_blkABCheckReady;
extern ABIgnite      g_blkABIgnite;
extern ABFlameConfirm g_blkABFlameConfirm;
extern ABStabilize   g_blkABStabilize;
extern SequenceEngine g_sequencer;
extern SequenceEngine g_abSequencer;
extern SafetyMonitor  g_safety;

// ============================================================
//  Hardware namespace — init / update functions called from main
// ============================================================

namespace Hardware {

    inline void applyCurrentSensorCal(AnalogLinearSensor& sensor, float zeroV, float mvPerA) {
        float safeZeroV = constrain(zeroV, 0.0f, 3.3f);
        float safeMvPerA = mvPerA > 0.0f ? mvPerA : 100.0f;
        float zeroAdc = (safeZeroV / 3.3f) * 4095.0f;
        float adcPerAmp = safeMvPerA / (3300.0f / 4095.0f);
        sensor.setCal({ zeroAdc, zeroAdc + adcPerAmp * 50.0f, 0.0f, 50.0f });
    }

    // ── Apply config values to block/controller instances ────
    inline void applyConfig() {
        // Startup blocks
        g_blkOilPrime.timeoutMs           = Config::startupOilArmTimeoutMs;
        g_blkOilPrime.oilArmMinBar        = Config::oilStartupMinBar;
        g_blkOilPrime.startupOilDemand    = Config::oilStartupPressure;
        g_blkOilPrime.startupOilPct       = Config::oilStartupPct;
        g_blkStarterSpin.starterDemand    = Config::starterDemand / 100.0f;
        g_blkStarterSpin.targetRpm        = Config::preIgnRpm;
        g_blkStarterSpin.timeoutMs        = (unsigned long)Config::starterTimeoutMs;
        g_blkStarterSpin.oilStartupMinBar = Config::oilStartupMinBar;
        g_blkStarterSpin.rampPctPerSec    = Config::starterRampPctPerSec;
        g_blkPreIgnSpark.sparkMs          = Config::preIgnSparkMs;
        g_blkTempConfirm.tempTarget       = Config::tempConfirmTarget;
        g_blkTempConfirm.timeoutMs        = (unsigned long)Config::tempConfirmTimeoutMs;
        g_blkWaitForInput.channelIdx      = Config::waitForInputChannel;
        g_blkWaitForInput.expectedState   = Config::waitForInputExpected;
        g_blkWaitForInput.timeoutMs       = (unsigned long)Config::waitForInputTimeoutMs;
        g_blkWaitForInputOff.channelIdx   = Config::waitForInputChannel;
        g_blkWaitForInputOff.expectedState= false;
        g_blkWaitForInputOff.timeoutMs    = (unsigned long)Config::waitForInputTimeoutMs;
        g_blkFlameConfirm.timeoutMs           = Config::flameTimeoutMs;
        g_blkFlameConfirm.checkIntervalMs     = Config::flameCheckIntervalMs;
        g_blkFlameConfirm.requiredCount       = Config::flameRequiredCount;
        g_blkFlameConfirm.turnOffIgniterOnExit= Config::flameConfirmTurnOffIgniter;
        g_blkTimedDelay.dwellMs               = (unsigned long)Config::timedDelayMs;
        g_blkFuelPulse.pulseMs               = (unsigned long)Config::fuelPulsePulseMs;
        g_blkFuelPulse.offMs                 = (unsigned long)Config::fuelPulseOffMs;
        g_blkWaitTOTCool.targetTot           = Config::waitTotCoolTarget;
        g_blkWaitTOTCool.timeoutMs           = (unsigned long)Config::waitTotCoolTimeoutMs;
        g_blkThrottleSet.pct                 = Config::throttleSetPct;
        g_blkPreHeat.preheatMs               = (unsigned long)Config::preHeatMs;
        g_blkOilPumpOn.demandPct             = Config::oilPumpOnPct;
        g_blkFuelPumpIdle.minPct              = Config::fuelPumpIdleMinPct;
        g_blkFuelPumpIdle.maxPct              = Config::fuelPumpIdleMaxPct;
        g_blkModifiedIdle.multiplier          = Config::modifiedIdleMultiplier;
        g_blkSpool.rpmTarget              = Config::spoolRpmTarget;
        g_blkSpool.timeoutMs              = Config::spoolTimeoutMs;
        g_blkSpool.throttleIdle           = Config::throttleIdleMinPct / 100.0f;
        g_blkSpool.cutStarterOnExit       = Config::spoolCutStarterOnExit;
        g_blkSpool.cutStarterEnOnExit     = Config::spoolCutStarterEnOnExit;
        g_blkSpool.runningOilMin          = Config::oilRunningMin;  // raise oil threshold at spool start
        g_blkSafetyHold.holdMs               = Config::safetyHoldMs;
        g_blkSafetyHold.finalCheckRpm        = Config::safetyHoldFinalRpm;
        g_blkSafetyHold.runningOilMin        = Config::oilRunningMin;
        g_blkSafetyHold.turnOffStarterOnExit  = Config::safetyHoldTurnOffStarter;
        g_blkSafetyHold.turnOffStarterEnOnExit= Config::safetyHoldTurnOffStarterEn;
        g_blkSafetyHold.turnOffIgniterOnExit  = Config::safetyHoldTurnOffIgniter;
        // Shutdown blocks
        g_blkRPMDrop.rpmThreshold         = Config::shutdownRpmDropThreshold;
        g_blkRPMDrop.timeoutMs            = Config::shutdownRpmDropTimeoutMs;
        g_blkCooldownSpin.totTarget          = Config::totCooldownTarget;
        g_blkCooldownSpin.starterCoolPct     = Config::cooldownStarterPct / 100.0f;
        g_blkCooldownSpin.oilCoolPct         = Config::cooldownOilPct;
        g_blkCooldownSpin.oilPressureTarget  = Config::cooldownOilPressureTarget;
        g_blkCooldownSpin.timeoutMs          = Config::shutdownCooldownTimeoutMs;
        g_blkFinalStop.timeoutMs            = Config::shutdownFinalStopTimeoutMs;
        g_blkFinalStop.rpmZeroThreshold    = Config::rpmZeroThreshold;
        g_blkFinalStop.oilScavengeMs       = (unsigned long)Config::finalStopOilScavengeMs;
        g_blkOilPrime.useScavengePump      = Config::oilPrimeUseScavengePump;
        g_blkCooldownSpin.useScavengePump  = Config::cooldownUseScavengePump;

        // AB blocks
        g_blkABCheckReady.minN1           = Config::abMinN1;
        g_blkABCheckReady.maxN1           = Config::abMaxN1;
        g_blkABCheckReady.maxTotForLight  = Config::abMaxTotForLight;
        g_blkABCheckReady.minThrottle     = (HardwareConfig::abTriggerSource == 1)
                                            ? Config::abThrottleThreshold : 0.0f;
        g_blkABIgnite.useTorch            = Config::abUseTorch;
        g_blkABIgnite.useIgniter          = Config::abUseIgniter;
        g_blkABIgnite.torchSpikePct       = Config::abTorchSpikePct;
        g_blkABIgnite.torchDurationMs     = Config::abTorchDurationMs;
        g_blkABIgnite.torchTotLimit       = Config::abTorchTotLimit;
        g_blkABPumpOn.demandPct           = Config::abLightupPumpPct;
        g_blkABFlameConfirm.flameMode     = Config::abFlameMode;
        g_blkABFlameConfirm.totRiseDegC   = Config::abTotRiseDegC;
        g_blkABFlameConfirm.totRiseWindowMs= Config::abTotRiseWindowMs;
        g_blkABFlameConfirm.assumeIgnitedMs= Config::abAssumeIgnitedMs;
        g_blkABFlameConfirm.flameTimeoutMs= Config::abFlameTimeoutMs;
        g_blkABStabilize.stabilizeMs      = Config::abStabilizeMs;
        g_blkABStabilize.stabilizeMaxTot  = Config::abStabilizeMaxTot;

        auto& hw = HardwareConfig::instance();
        if (hw.hasOilLoop) {
            g_ctrlOilLoop.adjustScale     = Config::oilAdjustScale;
            g_ctrlOilLoop.minPct          = Config::oilMinPct;
            g_ctrlOilLoop.failsafeDelayMs = Config::oilFailsafeDelayMs;
            g_ctrlOilLoop.failsafePct     = Config::oilFailsafePct;
            g_ctrlOilLoop.deadband        = Config::oilPressureDeadband;
        }
        if (hw.hasThrottleSlew) {
            g_ctrlThrottleSlew.rampUpMs     = Config::throttleRampUpMs;
            g_ctrlThrottleSlew.rampDownMs   = Config::throttleRampDownMs;
            g_ctrlThrottleSlew.n1PullbackEnabled = Config::pullbackN1Enabled;
            g_ctrlThrottleSlew.n2PullbackEnabled = Config::pullbackN2Enabled;
            g_ctrlThrottleSlew.egtPullbackEnabled = Config::pullbackEgtEnabled;
            g_ctrlThrottleSlew.rpmHardLimit = Config::pullbackN1HardRpm > 0.0f ? Config::pullbackN1HardRpm : Config::rpmLimit;
            g_ctrlThrottleSlew.rpmSoftLimit = Config::pullbackN1SoftRpm > 0.0f ? Config::pullbackN1SoftRpm : (Config::rpmLimit * 0.95f);
            g_ctrlThrottleSlew.n2HardLimit = Config::pullbackN2HardRpm;
            g_ctrlThrottleSlew.n2SoftLimit = Config::pullbackN2SoftRpm;
            const float egtLimit = Config::primaryEgtLimitC();
            g_ctrlThrottleSlew.totHardLimit = Config::pullbackEgtHardC > 0.0f ? Config::pullbackEgtHardC : egtLimit;
            g_ctrlThrottleSlew.totSoftLimit = Config::pullbackEgtSoftC > 0.0f ? Config::pullbackEgtSoftC : (egtLimit - Config::totSafeMargin);
            g_ctrlThrottleSlew.minPullbackThrottle = Config::pullbackMinThrottlePct / 100.0f;
            g_ctrlThrottleSlew.pullbackStrength = Config::pullbackStrength;
        }
        if (hw.hasDynamicIdle) {
            g_ctrlDynamicIdle.targetRpm     = Config::idleTargetRpm;
            g_ctrlDynamicIdle.rampUpMs      = Config::idleRampUpMs;
            g_ctrlDynamicIdle.rampDownMs    = Config::idleRampDownMs;
            g_ctrlDynamicIdle.deadbandRpm   = Config::idleDeadbandRpm;
            g_ctrlDynamicIdle.rpmLimit      = Config::idleRpmLimit;
            g_ctrlDynamicIdle.minMultiplier = Config::idleMinMultiplier;
        }
        if (hw.hasOilPress) {
            PolyCal pc;
            pc.a = Config::oilPolyA; pc.b = Config::oilPolyB;
            pc.c = Config::oilPolyC; pc.d = Config::oilPolyD;
            pc.xMin = Config::oilPolyXMin; pc.xMax = Config::oilPolyXMax;
            g_sensorOilPress.setCal(pc);
        }
        if (hw.hasFlame)  g_sensorFlame.setThreshold(Config::flameThreshold);
        if (hw.hasAbFlame && hw.abFlamePin >= 0) g_sensorAbFlame.setThreshold(hw.abFlameThreshold);
        if (hw.hasOilTemp && hw.oilTempPin >= 0 && strcmp(hw.oilTempChip, "ntc") == 0) {
            g_sensorOilTempNtc.setCal({ hw.ntcRFixed, hw.ntcR0, 25.0f, hw.ntcBeta });
        }
        if (hw.hasBattVoltage && hw.battVoltPin >= 0) {
            g_sensorBattVolt.setCal({ 0.0f, 4095.0f, 0.0f, hw.battVoltDivider * 3.3f });
        }
        if (hw.hasTorque && !hw.torqueHx711 && hw.torquePin >= 0) {
            g_sensorTorque.setCal({ 0.0f, 4095.0f, -hw.torqueOffset,
                                    hw.torqueScale * 3.3f - hw.torqueOffset });
        }
        if (hw.hasGlowCurrentSensor && hw.glowCurrentPin >= 0) {
            applyCurrentSensorCal(g_sensorGlowCurrent, hw.glowCurrentZeroV, hw.glowCurrentMvPerA);
        }
        if (hw.hasIgniterCurrentSensor && hw.igniterCurrentPin >= 0) {
            applyCurrentSensorCal(g_sensorIgniterCurrent, hw.igniterCurrentZeroV, hw.igniterCurrentMvPerA);
        }
        if (hw.hasIgniter2CurrentSensor && hw.igniter2CurrentPin >= 0) {
            applyCurrentSensorCal(g_sensorIgniter2Current, hw.igniter2CurrentZeroV, hw.igniter2CurrentMvPerA);
        }
        if (hw.hasOilPumpCurrentSensor && hw.oilPumpCurrentPin >= 0) {
            applyCurrentSensorCal(g_sensorOilPumpCurrent, hw.oilPumpCurrentZeroV, hw.oilPumpCurrentMvPerA);
        }
        if (hw.hasN1Rpm) { g_sensorN1Rpm.jumpThreshold  = Config::rpmJumpThreshold;
                           g_sensorN1Rpm.zeroStuckLimit = Config::rpmZeroStuckTicks;
                           g_sensorN1Rpm.rpmLimit       = Config::rpmLimit; }
        if (hw.hasN2Rpm) { g_sensorN2Rpm.jumpThreshold  = Config::rpmJumpThreshold;
                           g_sensorN2Rpm.zeroStuckLimit = Config::rpmZeroStuckTicks;
                           g_sensorN2Rpm.rpmLimit       = Config::rpmLimit; }
        g_safety.rpmLimit              = Config::rpmLimit;
        g_safety.minRpm               = Config::minRpm;
        g_safety.titLimit             = Config::titLimit;
        g_safety.oilTempLimit         = Config::oilTempLimit;
        g_safety.fuelPressMin         = Config::fuelPressMin;
        g_safety.battVoltMin          = Config::battVoltMin;
        g_safety.surgeRpmVariance     = Config::surgeDetectRpmVariance;
        g_safety.flameoutShutdownMs   = Config::flameoutShutdownMs;
        g_safety.flameoutSource       = Config::flameoutSource;
        g_safety.flameoutN1MinRpm     = Config::flameoutN1MinRpm;
        g_safety.flameoutTotDropC     = Config::flameoutTotDropC;
        g_safety.checkIntervalMs      = Config::safetyCheckIntervalMs;
        g_safety.totRiseRateLimit     = Config::totRiseRateLimitDegPerSec;

        if (hw.hasGovernor) {
            g_ctrlGovernor.targetRpm    = Config::governorTargetRpm;
            g_ctrlGovernor.bandRpm      = Config::governorBandRpm;
            g_ctrlGovernor.kp           = Config::governorKp;
            g_ctrlGovernor.pitchKp       = Config::governorPitchKp;
            g_ctrlGovernor.pitchRampSec  = Config::governorPitchRampSec;
            g_ctrlGovernor.usePropPitch  = hw.hasPropPitch && hw.propPitchType != 2 &&
                                           Config::governorPitchKp > 0.0f;
        }
        // Advanced sequence block params
        g_blkFuelPumpRamp.startPct      = Config::fp2StartPct;
        g_blkFuelPumpRamp.endPct        = Config::fp2EndPct;
        g_blkFuelPumpRamp.rampMs        = (unsigned long)Config::fp2RampMs;
        g_blkFuelPump2Set.demandPct     = Config::fp2DemandPct;
        g_blkGovernorHold.timeoutMs     = (unsigned long)Config::govHoldTimeoutMs;
        g_blkGovernorHold.bandRpm       = Config::governorBandRpm;

        // ADC linear-cal sensors — re-apply so PATCH calibration takes effect immediately
        if (hw.hasFuelFlow && hw.fuelFlowType != 1) {
            g_sensorFuelFlow.setCal({ (float)Config::fuelFlowRawMin,
                                      (float)Config::fuelFlowRawMax,
                                      0.0f, Config::fuelFlowValMax });
        }
        if (hw.hasFuelPress) {
            g_sensorFuelPress.setCal({ (float)Config::fuelPressRawMin,
                                       (float)Config::fuelPressRawMax,
                                       0.0f, Config::fuelPressValMax });
        }
        if (hw.hasP1) {
            g_sensorP1.setCal({ (float)Config::p1RawMin, (float)Config::p1RawMax,
                                0.0f, Config::p1ValMax });
        }
        if (hw.hasP2) {
            g_sensorP2.setCal({ (float)Config::p2RawMin, (float)Config::p2RawMax,
                                0.0f, Config::p2ValMax });
        }
    }

    // ── Sensor init ───────────────────────────────────────────
    inline void initSensors() {
        auto& hw = HardwareConfig::instance();
        if (hw.hasN1Rpm)   g_sensorN1Rpm.begin(hw.n1RpmPin, hw.n1RpmPpr);
        if (hw.hasN2Rpm)   g_sensorN2Rpm.begin(hw.n2RpmPin, hw.n2RpmPpr);
        if (hw.hasTot) {
            if (strncmp(hw.totChip, "max31856", 8) == 0) {
                g_sensorTot31856 = MAX31856TempSensor(hw.totClk, hw.totCs, hw.totMiso, hw.totMosi, hw.totTcType, "TOT_31856");
                g_sensorTot31856.begin(); g_pSensorTot = &g_sensorTot31856;
            } else if (strncmp(hw.totChip, "max31855", 8) == 0) {
                g_sensorTotAlt.begin(hw.totClk, hw.totCs, hw.totMiso);
                g_pSensorTot = &g_sensorTotAlt;
            } else {
                g_sensorTot.begin(hw.totClk, hw.totCs, hw.totMiso);
                g_pSensorTot = &g_sensorTot;
            }
        }
        if (hw.hasTit) {
            if (strncmp(hw.titChip, "max31856", 8) == 0) {
                g_sensorTit31856 = MAX31856TempSensor(hw.titClk, hw.titCs, hw.titMiso, hw.titMosi, hw.titTcType, "TIT_31856");
                g_sensorTit31856.begin(); g_pSensorTit = &g_sensorTit31856;
            } else if (strncmp(hw.titChip, "max31855", 8) == 0) {
                g_sensorTitAlt.begin(hw.titClk, hw.titCs, hw.titMiso);
                g_pSensorTit = &g_sensorTitAlt;
            } else {
                g_sensorTit.begin(hw.titClk, hw.titCs, hw.titMiso);
                g_pSensorTit = &g_sensorTit;
            }
        }
        if (hw.hasOilTemp && hw.oilTempPin >= 0) {
            if (strncmp(hw.oilTempChip, "max31856", 8) == 0) {
                g_sensorOilTemp856 = MAX31856TempSensor(hw.oilTempPin, hw.oilTempCs, hw.oilTempMiso,
                                                        hw.oilTempMosi, hw.oilTempTcType, "OIL_TEMP_856");
                g_sensorOilTemp856.begin(); g_pSensorOilTemp = &g_sensorOilTemp856;
            } else if (strncmp(hw.oilTempChip, "max31855", 8) == 0) {
                g_sensorOilTemp855.begin(hw.oilTempPin, hw.oilTempCs, hw.oilTempMiso);
                g_pSensorOilTemp = &g_sensorOilTemp855;
            } else if (strncmp(hw.oilTempChip, "max6675", 7) == 0) {
                g_sensorOilTempTc.begin(hw.oilTempPin, hw.oilTempCs, hw.oilTempMiso);
                g_pSensorOilTemp = &g_sensorOilTempTc;
            } else if (strncmp(hw.oilTempChip, "ds18b20", 7) == 0) {
                // DS18B20 OneWire digital thermometer — single data pin, no SPI.
                g_sensorOilTempDs18b20.begin(hw.oilTempPin,
                                             (uint8_t)constrain(hw.oilTempResolution, 9, 12));
                g_pSensorOilTemp = &g_sensorOilTempDs18b20;
            } else {
                // NTC analog — Steinhart-Hart B-parameter equation.
                g_sensorOilTempNtc.begin(hw.oilTempPin);
                g_sensorOilTempNtc.setCal({ hw.ntcRFixed, hw.ntcR0, 25.0f, hw.ntcBeta });
                g_pSensorOilTemp = &g_sensorOilTempNtc;
            }
        }
        if (hw.hasBattVoltage && hw.battVoltPin >= 0) {
            g_sensorBattVolt.begin(hw.battVoltPin);
            // ADC 0–4095 → 0–3.3 V; multiply by divider to get Vbatt
            g_sensorBattVolt.setCal({ 0.0f, 4095.0f, 0.0f, hw.battVoltDivider * 3.3f });
        }
        if (hw.hasTorque && hw.torqueHx711 && hw.torqueDtPin >= 0 && hw.torqueClkPin >= 0) {
            g_sensorTorqueHx711.begin(hw.torqueDtPin, hw.torqueClkPin,
                                      hw.torqueHxScale, (long)hw.torqueHxZero);
        } else if (hw.hasTorque && hw.torquePin >= 0) {
            g_sensorTorque.begin(hw.torquePin);
            // torqueOffset is the Nm-equivalent zero-load reading to subtract.
            g_sensorTorque.setCal({ 0.0f, 4095.0f, -hw.torqueOffset,
                                    hw.torqueScale * 3.3f - hw.torqueOffset });
        }
        if (hw.hasOilPress) g_sensorOilPress.begin(hw.oilPressPin);
        if (hw.hasFlame)   g_sensorFlame.begin(hw.flamePin);
        if (hw.hasIdleInput && !hw.idleInputRcPwm)
            g_sensorIdleInput.begin(hw.idleInputPin);
        if (hw.hasThrottleInput && !hw.throttleInputRcPwm)
            g_sensorThrottleInput.begin(hw.throttleInputPin);
        if (hw.hasFuelFlow) {
            if (hw.fuelFlowType == 1) {
                // Pulse / frequency type — reuse PCNT infrastructure (pulsesPerRev=1 → RPM = pulses/min)
                g_sensorFuelFlowPulse.begin(hw.fuelFlowPin, 1.0f);
            } else {
                // Analog voltage type
                g_sensorFuelFlow.begin(hw.fuelFlowPin);
                g_sensorFuelFlow.setCal({ (float)Config::fuelFlowRawMin,
                                          (float)Config::fuelFlowRawMax,
                                          0.0f, Config::fuelFlowValMax });
            }
        }
        if (hw.hasFuelPress) {
            g_sensorFuelPress.begin(hw.fuelPressPin);
            g_sensorFuelPress.setCal({ (float)Config::fuelPressRawMin,
                                       (float)Config::fuelPressRawMax,
                                       0.0f, Config::fuelPressValMax });
        }
        if (hw.hasP1) {
            g_sensorP1.begin(hw.p1Pin);
            g_sensorP1.setCal({ (float)Config::p1RawMin, (float)Config::p1RawMax,
                                 0.0f, Config::p1ValMax });
        }
        if (hw.hasP2) {
            g_sensorP2.begin(hw.p2Pin);
            g_sensorP2.setCal({ (float)Config::p2RawMin, (float)Config::p2RawMax,
                                 0.0f, Config::p2ValMax });
        }
        if (hw.hasAbFlame && hw.abFlamePin >= 0) {
            g_sensorAbFlame.begin(hw.abFlamePin);
            g_sensorAbFlame.setThreshold(hw.abFlameThreshold);
        }
        if (hw.hasAfterburner && hw.abInputPin >= 0 && !hw.abInputRcPwm &&
            (hw.abTriggerSource == 3 || Config::abPumpControlMode == 2))
            g_sensorAbInput.begin(hw.abInputPin);
        if (hw.abRequiresArmSwitch && hw.abArmSwitchPin >= 0)
            pinMode(hw.abArmSwitchPin, hw.abArmSwitchActiveH ? INPUT : INPUT_PULLUP);
        if (hw.abTriggerSource == 2 && hw.abSwitchPin >= 0)
            pinMode(hw.abSwitchPin, hw.abSwitchActiveH ? INPUT : INPUT_PULLUP);
        if (hw.hasGlowCurrentSensor && hw.glowCurrentPin >= 0) {
            g_sensorGlowCurrent.begin(hw.glowCurrentPin);
            applyCurrentSensorCal(g_sensorGlowCurrent, hw.glowCurrentZeroV, hw.glowCurrentMvPerA);
        }
        if (hw.hasIgniterCurrentSensor && hw.igniterCurrentPin >= 0) {
            g_sensorIgniterCurrent.begin(hw.igniterCurrentPin);
            applyCurrentSensorCal(g_sensorIgniterCurrent, hw.igniterCurrentZeroV, hw.igniterCurrentMvPerA);
        }
        if (hw.hasIgniter2CurrentSensor && hw.igniter2CurrentPin >= 0) {
            g_sensorIgniter2Current.begin(hw.igniter2CurrentPin);
            applyCurrentSensorCal(g_sensorIgniter2Current, hw.igniter2CurrentZeroV, hw.igniter2CurrentMvPerA);
        }
        if (hw.hasOilPumpCurrentSensor && hw.oilPumpCurrentPin >= 0) {
            g_sensorOilPumpCurrent.begin(hw.oilPumpCurrentPin);
            applyCurrentSensorCal(g_sensorOilPumpCurrent, hw.oilPumpCurrentZeroV, hw.oilPumpCurrentMvPerA);
        }
    }

    // ── Sensor update → EngineData ────────────────────────────
    inline void updateSensors() {
        auto& hw = HardwareConfig::instance();
        auto& ed = EngineData::instance();
        if (hw.hasN1Rpm) {
            g_sensorN1Rpm.update();
            ed.n1Rpm     = g_sensorN1Rpm.getValue();
            ed.n1Healthy = g_sensorN1Rpm.isHealthy();
        }
        if (hw.hasN2Rpm) {
            g_sensorN2Rpm.update();
            ed.n2Rpm     = g_sensorN2Rpm.getValue();
            ed.n2Healthy = g_sensorN2Rpm.isHealthy();
        }
        if (hw.hasTot && g_pSensorTot) {
            g_pSensorTot->update();
            ed.tot        = g_pSensorTot->getValue();
            ed.totHealthy = g_pSensorTot->isHealthy();
        }
        if (hw.hasTit && g_pSensorTit) {
            g_pSensorTit->update();
            ed.tit        = g_pSensorTit->getValue();
            ed.titHealthy = g_pSensorTit->isHealthy();
        }
        if (hw.hasOilPress) {
            g_sensorOilPress.update();
            ed.oilPressure    = g_sensorOilPress.getValue();
            ed.oilPressureRaw = g_sensorOilPress.rawCounts();
            ed.oilHealthy     = g_sensorOilPress.isHealthy();
        }
        if (hw.hasFlame) {
            g_sensorFlame.update();
            ed.flameSensorRaw = g_sensorFlame.rawCounts();
            ed.flameDetected  = g_sensorFlame.getValue() > 0.5f;
            // Wiring hint only — never gates flameDetected (see EngineData.h)
            ed.flameHealthy   = g_sensorFlame.railHealthy();
        }
        if (hw.hasIdleInput && !hw.idleInputRcPwm) {
            g_sensorIdleInput.update();
            ed.idleInputRaw = g_sensorIdleInput.rawCounts();
        }
        // Throttle input — ADC path; RC PWM path writes throttleInputRaw via RCInput::tick()
        if (hw.hasThrottleInput && !hw.throttleInputRcPwm) {
            g_sensorThrottleInput.update();
            ed.throttleInputRaw = g_sensorThrottleInput.rawCounts();
        }
        // AB flame sensor (dedicated, optional)
        if (hw.hasAbFlame && hw.abFlamePin >= 0) {
            g_sensorAbFlame.update();
            ed.abFlameOn = (g_sensorAbFlame.getValue() > 0.5f);
        }
        // AB analog/RC trigger input
        if (hw.hasAfterburner && hw.abInputPin >= 0 && !hw.abInputRcPwm &&
            (hw.abTriggerSource == 3 || Config::abPumpControlMode == 2)) {
            g_sensorAbInput.update();
            ed.abInputRaw = g_sensorAbInput.rawCounts();
            ed.abInputNorm = constrain(ed.abInputRaw / 4095.0f, 0.0f, 1.0f);
            ed.abInputValid = true;
        }
        // AB arm switch
        if (hw.abRequiresArmSwitch && hw.abArmSwitchPin >= 0) {
            bool pressed = (digitalRead(hw.abArmSwitchPin) ==
                            (hw.abArmSwitchActiveH ? HIGH : LOW));
            ed.abArmSwitchOn = pressed;
        }
        // ── New sensors ──────────────────────────────────────────
        if (hw.hasOilTemp && g_pSensorOilTemp) {
            g_pSensorOilTemp->update();
            ed.oilTemp        = g_pSensorOilTemp->getValue();
            ed.oilTempHealthy = g_pSensorOilTemp->isHealthy();
            if (ed.oilTempHealthy && ed.oilTemp > ed.maxOilTemp) ed.maxOilTemp = ed.oilTemp;
        }
        if (hw.hasBattVoltage) {
            g_sensorBattVolt.update();
            ed.battVoltage  = g_sensorBattVolt.getValue();
            ed.battHealthy  = g_sensorBattVolt.isHealthy();
            if (ed.battHealthy && ed.battVoltage > ed.maxBattVoltage) ed.maxBattVoltage = ed.battVoltage;
        }
        if (hw.hasTorque) {
            if (hw.torqueHx711) {
                g_sensorTorqueHx711.update();
                ed.torque        = g_sensorTorqueHx711.getValue();
                ed.torqueRaw     = (int)g_sensorTorqueHx711.rawCounts();
                ed.torqueHealthy = g_sensorTorqueHx711.isHealthy();
            } else {
                g_sensorTorque.update();
                ed.torque        = g_sensorTorque.getValue();
                ed.torqueRaw     = g_sensorTorque.rawCounts();
                ed.torqueHealthy = g_sensorTorque.isHealthy();
            }
            // shaft power = torque × angular velocity of N2
            if (ed.torqueHealthy && ed.n2Healthy && ed.n2Rpm > 0) {
                float omega = ed.n2Rpm * (2.0f * 3.14159f / 60.0f); // rad/s
                ed.turboPower = ed.torque * omega;
            } else {
                ed.turboPower = 0.0f;
            }
        }
        if (hw.hasFuelFlow) {
            if (hw.fuelFlowType == 1) {
                g_sensorFuelFlowPulse.update();
                // RPM = pulses/min; divide by pulsesPerLitre → litres/min
                float ppl = hw.fuelFlowPulsesPerLitre > 0 ? hw.fuelFlowPulsesPerLitre : 1.0f;
                ed.fuelFlow = g_sensorFuelFlowPulse.getValue() / ppl;
                ed.fuelFlowHealthy = g_sensorFuelFlowPulse.isHealthy();
            } else {
                g_sensorFuelFlow.update();
                ed.fuelFlow = g_sensorFuelFlow.getValue();
                ed.fuelFlowHealthy = g_sensorFuelFlow.isHealthy();
            }
        }
        if (hw.hasP1) {
            g_sensorP1.update();
            ed.p1 = g_sensorP1.getValue();
            ed.p1Healthy = g_sensorP1.isHealthy();
        }
        if (hw.hasP2) {
            g_sensorP2.update();
            ed.p2 = g_sensorP2.getValue();
            ed.p2Healthy = g_sensorP2.isHealthy();
        }
        if (hw.hasFuelPress) {
            g_sensorFuelPress.update();
            ed.fuelPressure     = g_sensorFuelPress.getValue();
            ed.fuelPressRaw     = g_sensorFuelPress.rawCounts();
            ed.fuelPressHealthy = g_sensorFuelPress.isHealthy();
        }
        if (hw.hasGlowCurrentSensor) {
            g_sensorGlowCurrent.update();
            ed.glowCurrentAmps = g_sensorGlowCurrent.getValue();
            // Plug is hot when current has dropped below threshold and plug is
            // powered.  Health gate: a disconnected/railed ADC reads ~0 A and
            // would instantly flag a cold plug 'hot' (GlowPreheat has its own
            // waitHotTimeout, so an unhealthy sensor cannot hang the sequence).
            ed.glowPlugHot = g_sensorGlowCurrent.isHealthy() &&
                             (ed.glowPlugDemand > 0.05f) &&
                             (ed.glowCurrentAmps <= hw.glowCurrentReadyAmps);
        }
        if (hw.hasIgniterCurrentSensor) {
            g_sensorIgniterCurrent.update();
            ed.igniterCurrentAmps = g_sensorIgniterCurrent.getValue();
        }
        if (hw.hasIgniter2CurrentSensor) {
            g_sensorIgniter2Current.update();
            ed.igniter2CurrentAmps = g_sensorIgniter2Current.getValue();
        }
        if (hw.hasOilPumpCurrentSensor) {
            g_sensorOilPumpCurrent.update();
            ed.oilPumpCurrentAmps = g_sensorOilPumpCurrent.getValue();
            ed.oilPumpOvercurrent = (hw.oilPumpCurrentMaxAmps > 0.0f)
                                    && (ed.oilPumpCurrentAmps > hw.oilPumpCurrentMaxAmps);
        }
    }

    // ── Boot-safe relay states ────────────────────────────────
    // Drive the RUNTIME-configured fuel solenoid / igniter(s) / starter-enable
    // pins to their inactive level.  PlatformInit::begin() parks the compile-time
    // OT_* pins as the first line of defense, but the config may remap these
    // outputs, leaving the real pin floating until initActuators().  Called from
    // setup() immediately after HardwareConfig::load() succeeds.
    inline void driveBootSafeStates() {
        auto& hw = HardwareConfig::instance();
        auto driveInactive = [](int pin, bool activeH) {
            if (pin < 0) return;
            pinMode(pin, OUTPUT);
            digitalWrite(pin, activeH ? LOW : HIGH);
        };
        if (hw.hasFuelSol)   driveInactive(hw.fuelSolPin, hw.fuelSolActiveH);
        // PWM igniter drive is active-high (LEDC duty 0 = pin LOW);
        // igniterActiveH applies in relay/coil mode only.
        if (hw.hasIgniter)   driveInactive(hw.igniterPin,
                                           hw.igniterPwm ? true : hw.igniterActiveH);
        if (hw.hasIgniter2)  driveInactive(hw.igniter2Pin,
                                           hw.igniter2Pwm ? true : hw.igniter2ActiveH);
        if (hw.hasGlowPlug)  driveInactive(hw.glowPlugPin,
                                           hw.glowPlugOutputType == 1 ? hw.glowPlugActiveH : true);
        if (hw.hasStarterEn) driveInactive(hw.starterEnPin, hw.starterEnActiveH);
    }

    // ── Actuator init ─────────────────────────────────────────
    inline void initActuators() {
        auto& hw = HardwareConfig::instance();
        if (hw.hasThrottle) {
            if (hw.throttleType == 1) {
                g_actThrottleLedc.setInverted(hw.throttleInverted);
                g_actThrottleLedc.setOutputRange(hw.throttlePwmMinPct, hw.throttlePwmMaxPct);
                g_actThrottleLedc.begin(hw.throttlePin, (uint32_t)hw.throttleLedcFreqHz, (uint8_t)hw.throttleLedcBits);
                g_actThrottle = &g_actThrottleLedc;
            } else if (hw.throttleType == 2) {
                g_actThrottleOnOff.begin(hw.throttlePin, hw.throttleActiveH);
                g_actThrottle = &g_actThrottleOnOff;
            } else {
                g_actThrottleServo.begin(hw.throttlePin, hw.throttleMinUs, hw.throttleMaxUs);
                g_actThrottle = &g_actThrottleServo;
            }
        }
        if (hw.hasStarter) {
            if (hw.starterType == 1) {
                g_actStarterLedc.setInverted(hw.starterInverted);
                g_actStarterLedc.setOutputRange(hw.starterPwmMinPct, hw.starterPwmMaxPct);
                g_actStarterLedc.begin(hw.starterPin, (uint32_t)hw.starterLedcFreqHz, (uint8_t)hw.starterLedcBits);
                g_actStarter = &g_actStarterLedc;
            } else if (hw.starterType == 2) {
                g_actStarterOnOff.begin(hw.starterPin, hw.starterActiveH);
                g_actStarter = &g_actStarterOnOff;
            } else {
                g_actStarterServo.begin(hw.starterPin, hw.starterMinUs, hw.starterMaxUs);
                g_actStarter = &g_actStarterServo;
            }
        }
        if (hw.hasOilPump) {
            if (hw.oilPumpType == 2) {
                g_actOilPumpRelay.begin(hw.oilPumpPin, hw.oilPumpActiveH);
                g_actOilPump = &g_actOilPumpRelay;
            } else if (hw.oilPumpType == 0) {
                g_actOilPumpServo.begin(hw.oilPumpPin, hw.oilPumpMinUs, hw.oilPumpMaxUs);
                g_actOilPump = &g_actOilPumpServo;
            } else {
                g_actOilPumpLedc.setOutputRange(hw.oilPumpPwmMinPct, hw.oilPumpPwmMaxPct);
                g_actOilPumpLedc.begin(hw.oilPumpPin,
                                       (uint32_t)hw.oilPumpFreqHz,
                                       (uint8_t)hw.oilPumpResBits);
                g_actOilPump = &g_actOilPumpLedc;
            }
        }
        if (hw.hasFuelSol)
            g_actFuelSol.begin(hw.fuelSolPin, hw.fuelSolActiveH);
        if (hw.hasIgniter) {
            if (hw.igniterPwm) {
                int period = hw.igniterDwellMs + hw.igniterRestMs;
                uint32_t freq = (period > 0) ? (1000u / (uint32_t)period) : 111u;
                g_actIgniterLedc.begin(hw.igniterPin, freq, 8);
                g_actIgniter = &g_actIgniterLedc;
            } else {
                g_actIgniterRelay.begin(hw.igniterPin, hw.igniterActiveH);
                g_actIgniter = &g_actIgniterRelay;
            }
        }
        if (hw.hasIgniter2 && hw.igniter2Pin >= 0) {
            if (hw.igniter2Pwm) {
                int period = hw.igniter2DwellMs + hw.igniter2RestMs;
                uint32_t freq = (period > 0) ? (1000u / (uint32_t)period) : 111u;
                g_actIgniter2Ledc.begin(hw.igniter2Pin, freq, 8);
                g_actIgniter2 = &g_actIgniter2Ledc;
            } else {
                g_actIgniter2Relay.begin(hw.igniter2Pin, hw.igniter2ActiveH);
                g_actIgniter2 = &g_actIgniter2Relay;
            }
        }
        if (hw.hasStarterEn)
            g_actStarterEn.begin(hw.starterEnPin, hw.starterEnActiveH);
        if (hw.hasAbSol && hw.abSolPin >= 0)
            g_actAbSol.begin(hw.abSolPin, hw.abSolActiveH);
        if (hw.hasAirstarterSol && hw.airstarterSolPin >= 0)
            g_actAirstarterSol.begin(hw.airstarterSolPin, hw.airstarterSolActiveH);
        if (hw.hasCoolFan && hw.coolFanPin >= 0) {
            if (hw.coolFanType == 0) {
                g_actCoolFanServo.begin(hw.coolFanPin, hw.coolFanMinUs, hw.coolFanMaxUs);
                g_pActCoolFan = &g_actCoolFanServo;
            } else if (hw.coolFanType == 1) {
                g_actCoolFanLedc.setOutputRange(hw.coolFanPwmMinPct, hw.coolFanPwmMaxPct);
                g_actCoolFanLedc.begin(hw.coolFanPin, (uint32_t)hw.coolFanFreqHz, (uint8_t)hw.coolFanResBits);
                g_pActCoolFan = &g_actCoolFanLedc;
            } else {
                g_actCoolFan.begin(hw.coolFanPin, hw.coolFanActiveH);
                g_pActCoolFan = &g_actCoolFan;
            }
        }
        if (hw.hasAbPump && hw.abPumpPin >= 0) {
            if (hw.abPumpType == 0) {
                g_actAbPumpServo.begin(hw.abPumpPin, hw.abPumpMinUs, hw.abPumpMaxUs);
                g_actAbPump = &g_actAbPumpServo;
            } else if (hw.abPumpType == 1) {
                g_actAbPumpLedc.setOutputRange(hw.abPumpPwmMinPct, hw.abPumpPwmMaxPct);
                g_actAbPumpLedc.begin(hw.abPumpPin, (uint32_t)hw.abPumpFreqHz, (uint8_t)hw.abPumpResBits);
                g_actAbPump = &g_actAbPumpLedc;
            } else {
                g_actAbPumpRelay.begin(hw.abPumpPin, hw.abPumpActiveH);
                g_actAbPump = &g_actAbPumpRelay;
            }
        }
        if (hw.hasOilScavengePump && hw.oilScavPumpPin >= 0) {
            if (hw.oilScavPumpType == 0) {
                g_actOilScavServo.begin(hw.oilScavPumpPin,
                                        hw.oilScavPumpMinUs,
                                        hw.oilScavPumpMaxUs);
                g_actOilScavPump = &g_actOilScavServo;
            } else if (hw.oilScavPumpType == 1) {
                g_actOilScavLedc.setOutputRange(hw.oilScavPumpPwmMinPct, hw.oilScavPumpPwmMaxPct);
                g_actOilScavLedc.begin(hw.oilScavPumpPin,
                                       (uint32_t)hw.oilScavPumpFreqHz,
                                       (uint8_t)hw.oilScavPumpResBits);
                g_actOilScavPump = &g_actOilScavLedc;
            } else {
                g_actOilScavRelay.begin(hw.oilScavPumpPin, hw.oilScavPumpActiveH);
                g_actOilScavPump = &g_actOilScavRelay;
            }
        }
        if (hw.hasFuelPump2 && hw.fuelPump2Pin >= 0) {
            if (hw.fuelPump2Type == 2) {
                g_actFuelPump2Relay.begin(hw.fuelPump2Pin, hw.fuelPump2ActiveH);
                g_actFuelPump2 = &g_actFuelPump2Relay;
            } else if (hw.fuelPump2Type == 0) {
                g_actFuelPump2Servo.begin(hw.fuelPump2Pin, hw.fuelPump2MinUs, hw.fuelPump2MaxUs);
                g_actFuelPump2 = &g_actFuelPump2Servo;
            } else {
                g_actFuelPump2Ledc.setOutputRange(hw.fuelPump2PwmMinPct, hw.fuelPump2PwmMaxPct);
                g_actFuelPump2Ledc.begin(hw.fuelPump2Pin,
                                         (uint32_t)hw.fuelPump2FreqHz,
                                         (uint8_t)hw.fuelPump2ResBits);
                g_actFuelPump2 = &g_actFuelPump2Ledc;
            }
        }
        if (hw.hasBleedValve && hw.bleedValvePin >= 0) {
            if (hw.bleedValveType == 1) {
                g_actBleedValveServo.begin(hw.bleedValvePin, hw.bleedValveMinUs, hw.bleedValveMaxUs);
                g_actBleedValve = &g_actBleedValveServo;
            } else if (hw.bleedValveType == 2) {
                g_actBleedValveLedc.setOutputRange(hw.bleedValvePwmMinPct, hw.bleedValvePwmMaxPct);
                g_actBleedValveLedc.begin(hw.bleedValvePin, (uint32_t)hw.bleedValveFreqHz, (uint8_t)hw.bleedValveResBits);
                g_actBleedValve = &g_actBleedValveLedc;
            } else {
                g_actBleedValveRelay.begin(hw.bleedValvePin, hw.bleedValveActiveH);
                g_actBleedValve = &g_actBleedValveRelay;
            }
        }
        if (hw.hasPropPitch && hw.propPitchPin >= 0) {
            if (hw.propPitchType == 1) {
                g_actPropPitchLedc.setOutputRange(hw.propPitchPwmMinPct, hw.propPitchPwmMaxPct);
                g_actPropPitchLedc.begin(hw.propPitchPin, (uint32_t)hw.propPitchFreqHz, (uint8_t)hw.propPitchResBits);
                g_actPropPitch = &g_actPropPitchLedc;
            } else if (hw.propPitchType == 2) {
                g_actPropPitchRelay.begin(hw.propPitchPin, hw.propPitchActiveH);
                g_actPropPitch = &g_actPropPitchRelay;
            } else {
                g_actPropPitchServo.begin(hw.propPitchPin, hw.propPitchMinUs, hw.propPitchMaxUs);
                g_actPropPitch = &g_actPropPitchServo;
            }
        }
        if (hw.hasGlowPlug && hw.glowPlugPin >= 0) {
            if (hw.glowPlugOutputType == 1) {
                g_actGlowPlugRelay.begin(hw.glowPlugPin, hw.glowPlugActiveH);
            } else {
                g_actGlowPlug.setOutputRange(hw.glowPlugPwmMinPct, hw.glowPlugPwmMaxPct);
                g_actGlowPlug.begin(hw.glowPlugPin, (uint32_t)hw.glowPlugFreqHz,
                                    (uint8_t)hw.glowPlugResBits);
            }
        }
        if (hw.hasGlowPlug && hw.glowPlugType == 2 && hw.wetGlowFuelPin >= 0) {
            if (hw.wetGlowFuelType == 2) {
                g_actWetGlowFuelServo.begin(hw.wetGlowFuelPin, hw.wetGlowFuelMinUs, hw.wetGlowFuelMaxUs);
                g_actWetGlowFuel = &g_actWetGlowFuelServo;
            } else if (hw.wetGlowFuelType == 1) {
                g_actWetGlowFuelLedc.setOutputRange(hw.wetGlowFuelPwmMinPct, hw.wetGlowFuelPwmMaxPct);
                g_actWetGlowFuelLedc.begin(hw.wetGlowFuelPin, (uint32_t)hw.wetGlowFuelFreqHz,
                                           (uint8_t)hw.wetGlowFuelResBits);
                g_actWetGlowFuel = &g_actWetGlowFuelLedc;
            } else {
                g_actWetGlowFuelRelay.begin(hw.wetGlowFuelPin, hw.wetGlowFuelActiveH);
                g_actWetGlowFuel = &g_actWetGlowFuelRelay;
            }
        }
    }

    // ── Actuator update: EngineData demands → physical signals ─
    inline void updateActuators() {
        auto& hw = HardwareConfig::instance();
        auto& ed = EngineData::instance();
        // AB main-fuel offset is added here at the actuator write, NOT to throttleDemand,
        // so ThrottleSlew's feedback loop never sees the inflated value.
        if (hw.hasThrottle && g_actThrottle)
            g_actThrottle->set(constrain(ed.throttleDemand + ed.abFuelOffset, 0.0f, 1.0f));

        // Starter enable relay
        if (hw.hasStarterEn) {
            g_actStarterEn.set(ed.starterEnabled ? 1.0f : 0.0f);
        }
        // Only allow starter to spin once the enable relay is on and its
        // delay has elapsed — with the relay off, the demand must not reach
        // the ESC/motor (the relay may not be the sole power gate).
        if (hw.hasStarter && g_actStarter) {
            static bool  _prevEn2 = false;
            static unsigned long _enMs2 = 0;
            if (ed.starterEnabled && !_prevEn2) _enMs2 = millis();
            _prevEn2 = ed.starterEnabled;
            bool delayOk = !hw.hasStarterEn ||
                           (ed.starterEnabled &&
                            ((millis() - _enMs2) >= (unsigned long)hw.starterEnDelayMs));
            g_actStarter->set(delayOk ? ed.starterDemand : 0.0f);
        }
        if (hw.hasAbPump      && g_actAbPump)      g_actAbPump->set(ed.abPumpDemand);
        if (hw.hasAbSol)         g_actAbSol.set(ed.abSolOpen ? 1.0f : 0.0f);
        if (hw.hasAirstarterSol) g_actAirstarterSol.set(ed.airstarterOpen ? 1.0f : 0.0f);
        if (hw.hasCoolFan && g_pActCoolFan)  g_pActCoolFan->set(ed.coolFanOn ? 1.0f : 0.0f);
        if (hw.hasOilScavengePump && g_actOilScavPump)
            g_actOilScavPump->set(ed.oilScavengeOn ? 1.0f : 0.0f);
        if (hw.hasOilPump && g_actOilPump) {
            float demand = (hw.oilPumpType == 2)
                         ? (ed.oilPumpPct > 0.0f ? 1.0f : 0.0f)
                         : (ed.oilPumpPct / 100.0f);
            g_actOilPump->set(demand);
        }
        if (hw.hasFuelSol) g_actFuelSol.set(ed.fuelSolOpen ? 1.0f : 0.0f);
        if (hw.hasIgniter && g_actIgniter) {
            if (hw.igniterCoil) {
                static bool     s_coilCharging   = false;
                static uint32_t s_coilPhaseStart = 0;
                if (ed.igniterOn) {
                    uint32_t now = millis();
                    // Dwell time is always the hard cap on charge duration;
                    // the current sensor only ends the charge early at coil
                    // saturation.  Without the cap, a failed-low sensor (or a
                    // weak supply never reaching satAmps) would leave the coil
                    // energized continuously, overheating coil and driver.
                    bool endCharge = (now - s_coilPhaseStart) >= (uint32_t)hw.igniterDwellMs;
                    if (hw.hasIgniterCurrentSensor &&
                        ed.igniterCurrentAmps >= hw.igniterCoilSatAmps)
                        endCharge = true;
                    if (s_coilCharging) {
                        if (endCharge) {
                            s_coilCharging   = false;
                            s_coilPhaseStart = now;
                            g_actIgniter->set(0.0f);
                        }
                    } else {
                        if ((now - s_coilPhaseStart) >= (uint32_t)hw.igniterRestMs) {
                            s_coilCharging   = true;
                            s_coilPhaseStart = now;
                            g_actIgniter->set(1.0f);
                        }
                    }
                } else {
                    // Reset phase timer to now so the rest period is measured
                    // from when the coil was actually switched off, not from
                    // the start of the previous charge phase.  Without this,
                    // a rapid off→on toggle could restart charging before the
                    // full rest period has elapsed.
                    s_coilCharging   = false;
                    s_coilPhaseStart = (uint32_t)millis();
                    g_actIgniter->set(0.0f);
                }
            } else {
                float duty = hw.igniterPwm
                    ? ((hw.igniterDwellMs + hw.igniterRestMs > 0)
                       ? ((float)hw.igniterDwellMs / (hw.igniterDwellMs + hw.igniterRestMs))
                       : 0.5f)
                    : 1.0f;
                g_actIgniter->set(ed.igniterOn ? duty : 0.0f);
            }
        }
        if (hw.hasIgniter2 && g_actIgniter2) {
            if (hw.igniter2Coil) {
                static bool     s_coil2Charging   = false;
                static uint32_t s_coil2PhaseStart = 0;
                if (ed.igniter2On) {
                    uint32_t now = millis();
                    // Same dwell hard cap as igniter 1 — current sensing only
                    // ends the charge early, never extends it.
                    bool endCharge = (now - s_coil2PhaseStart) >= (uint32_t)hw.igniter2DwellMs;
                    if (hw.hasIgniter2CurrentSensor &&
                        ed.igniter2CurrentAmps >= hw.igniter2CoilSatAmps)
                        endCharge = true;
                    if (s_coil2Charging) {
                        if (endCharge) {
                            s_coil2Charging   = false;
                            s_coil2PhaseStart = now;
                            g_actIgniter2->set(0.0f);
                        }
                    } else {
                        if ((now - s_coil2PhaseStart) >= (uint32_t)hw.igniter2RestMs) {
                            s_coil2Charging   = true;
                            s_coil2PhaseStart = now;
                            g_actIgniter2->set(1.0f);
                        }
                    }
                } else {
                    s_coil2Charging   = false;
                    s_coil2PhaseStart = (uint32_t)millis();
                    g_actIgniter2->set(0.0f);
                }
            } else {
                float duty2 = hw.igniter2Pwm
                    ? ((hw.igniter2DwellMs + hw.igniter2RestMs > 0)
                       ? ((float)hw.igniter2DwellMs / (hw.igniter2DwellMs + hw.igniter2RestMs))
                       : 0.5f)
                    : 1.0f;
                g_actIgniter2->set(ed.igniter2On ? duty2 : 0.0f);
            }
        }
        if (hw.hasFuelPump2 && g_actFuelPump2)
            g_actFuelPump2->set(ed.fuelPump2Demand);
        if (hw.hasBleedValve && g_actBleedValve)
            g_actBleedValve->set(ed.bleedValveOpen ? 1.0f : 0.0f);
        if (hw.hasPropPitch && g_actPropPitch)
            g_actPropPitch->set(ed.propPitchDemand);
        if (hw.hasGlowPlug) {
            float glowDemand = constrain(ed.glowPlugDemand, 0.0f, 1.0f);
            if (hw.glowPlugOutputType == 1)
                g_actGlowPlugRelay.setOn(glowDemand > 0.001f);
            else
                g_actGlowPlug.set(glowDemand);
            if (hw.glowPlugType == 2 && g_actWetGlowFuel) {
                static bool s_wetGlowActive = false;
                static unsigned long s_wetGlowOnMs = 0;
                bool commandOn = glowDemand > 0.001f;
                if (commandOn && !s_wetGlowActive) {
                    s_wetGlowActive = true;
                    s_wetGlowOnMs = millis();
                    ed.wetGlowFuelDemand = 0.0f;
                    g_actWetGlowFuel->off();
                } else if (!commandOn) {
                    s_wetGlowActive = false;
                    s_wetGlowOnMs = 0;
                    ed.wetGlowFuelDemand = 0.0f;
                    g_actWetGlowFuel->off();
                }
                if (commandOn && s_wetGlowActive &&
                    (millis() - s_wetGlowOnMs) >= (unsigned long)hw.wetGlowFuelDelayMs) {
                    float fuelDemand = hw.wetGlowFuelType == 0 ? 1.0f : (hw.wetGlowFuelDemandPct / 100.0f);
                    ed.wetGlowFuelDemand = constrain(fuelDemand, 0.0f, 1.0f);
                    g_actWetGlowFuel->set(ed.wetGlowFuelDemand);
                }
            } else {
                ed.wetGlowFuelDemand = 0.0f;
            }
        }
    }

    // ── Emergency all-off ─────────────────────────────────────
    inline void allOff() {
        auto& hw = HardwareConfig::instance();
        if (hw.hasThrottle && g_actThrottle)  g_actThrottle->off();
        if (hw.hasStarter  && g_actStarter)   g_actStarter->off();
        if (hw.hasOilPump && g_actOilPump)    g_actOilPump->off();
        if (hw.hasFuelSol)                    g_actFuelSol.off();
        if (hw.hasIgniter && g_actIgniter)    g_actIgniter->off();
        if (hw.hasIgniter2 && g_actIgniter2)  g_actIgniter2->off();
        if (hw.hasStarterEn)                  g_actStarterEn.off();
        if (hw.hasAbPump && g_actAbPump)       g_actAbPump->off();
        if (hw.hasAbSol)                        g_actAbSol.off();
        if (hw.hasAirstarterSol)               g_actAirstarterSol.off();
        if (hw.hasCoolFan && g_pActCoolFan)    g_pActCoolFan->off();
        if (hw.hasOilScavengePump && g_actOilScavPump) g_actOilScavPump->off();
        if (hw.hasFuelPump2 && g_actFuelPump2) g_actFuelPump2->off();
        if (hw.hasBleedValve && g_actBleedValve)  g_actBleedValve->off();
        if (hw.hasPropPitch  && g_actPropPitch)   g_actPropPitch->set(0.0f);  // return to fine pitch
        if (hw.hasGlowPlug) {
            if (hw.glowPlugOutputType == 1) g_actGlowPlugRelay.off();
            else g_actGlowPlug.off();
        }
        if (hw.hasGlowPlug && hw.glowPlugType == 2 && g_actWetGlowFuel) g_actWetGlowFuel->off();
        auto& _ed = EngineData::instance();
        _ed.throttleDemand  = 0;
        _ed.fuelSolOpen     = false;
        _ed.igniterOn       = false;
        _ed.starterDemand   = 0;
        _ed.starterEnabled  = false;
        _ed.oilPumpPct      = 0;
        _ed.oilScavengeOn   = false;
        _ed.abSolOpen       = false;
        _ed.abPumpDemand    = 0;
        _ed.fuelPump2Demand  = 0;
        _ed.propPitchDemand  = 0;
        _ed.abFuelOffset     = 0.0f;
        _ed.bleedValveOpen   = false;
        _ed.glowPlugDemand   = 0;
        _ed.wetGlowFuelDemand = 0;
        _ed.surgeDetected    = false;
        _ed.igniter2On      = false;
        _ed.abMode          = ABMode::Off;
        _ed.airstarterOpen  = false;
        _ed.coolFanOn       = false;
    }

    // ── Status LED init / tick ────────────────────────────────
    inline void initStatusLED() {
        auto& hw = HardwareConfig::instance();
        // Respect hasStatusLed on every platform. On the S3 this used to run
        // unconditionally, so disabling the status LED in the hardware config had
        // no effect and the onboard NeoPixel stayed lit. When disabled, actively
        // clear the LED (it latches its last colour otherwise).
        if (hw.hasStatusLed) StatusLED::begin();
        else                 StatusLED::off();
    }
    inline void tickStatusLED() {
        if (HardwareConfig::instance().hasStatusLed) StatusLED::tick();
    }

    // ── Controller init ───────────────────────────────────────
    inline void initControllers() {
        auto& hw = HardwareConfig::instance();
        if (hw.hasOilLoop)      g_ctrlOilLoop.begin();
        if (hw.hasThrottleSlew) g_ctrlThrottleSlew.begin();
        if (hw.hasDynamicIdle)  g_ctrlDynamicIdle.begin();
        if (hw.hasGovernor)     g_ctrlGovernor.begin();
    }

    // ── Controller tick (RUNNING + late STARTUP) ──────────────
    inline void runControllers() {
        auto& hw   = HardwareConfig::instance();
        auto& ed   = EngineData::instance();
        auto  mode = ed.mode;
        if (mode != SysMode::RUNNING && mode != SysMode::STARTUP) return;

        // ── Pilot throttle input → demand mapping ──────────────
        // When a physical throttle input is configured (ADC pot or RC stick),
        // map it directly to throttleDemand in RUNNING mode.  DynamicIdle then
        // applies a floor on top, and ThrottleSlew rate-limits the result.
        // A throttle-primary governor (turboshaft/APU with no prop-pitch authority)
        // OWNS throttleDemand: it holds N2 by accumulating throttle over many ticks.
        // Re-mapping the pilot input onto throttleDemand every tick would wipe that
        // accumulation, so skip the input mapping while such a governor is active
        // (this is the "governor overrides this demand" contract). A pitch-primary
        // governor instead leaves the throttle to the pilot and holds N2 with pitch,
        // so the input mapping still applies there. DynamicIdle (ticked after the
        // governor) still enforces the running idle floor either way.
        const bool governorOwnsThrottle = hw.hasGovernor && hw.hasN2Rpm &&
                                          Config::governorTargetRpm > 0.0f &&
                                          !g_ctrlGovernor.usePropPitch;
        if (hw.hasThrottleInput && mode == SysMode::RUNNING && !governorOwnsThrottle) {
            float norm;
            if (hw.throttleInputRcPwm) {
                norm = (ed.rcThrottleValid) ? ed.rcThrottleNorm : 0.0f;
            } else {
                int range = Config::throttleMaxRaw - Config::throttleMinRaw;
                norm = (range != 0)
                    ? constrain((ed.throttleInputRaw - Config::throttleMinRaw) /
                                (float)range, 0.0f, 1.0f)
                    : 0.0f;
            }
            // Apply throttle expo if configured (softens stick sensitivity)
            float expo = Config::throttleExpo;  // 0=linear, 1=max expo
            if (expo > 0.0f && expo < 1.0f) {
                // Standard RC expo: y = x*(1-e) + x^3*e
                norm = norm * (1.0f - expo) + norm * norm * norm * expo;
            }
            // Map from idle floor to full range
            float minPct = Config::throttleIdleMinPct / 100.0f;
            ed.throttleDemand = constrain(minPct + norm * (1.0f - minPct), 0.0f, 1.0f);
        }

        if (hw.hasOilLoop) {
            if (mode == SysMode::RUNNING) {
                if (Config::oilUseThrottleMap) {
                    float t  = constrain(ed.throttleDemand, 0.0f, 1.0f);
                    ed.oilTargetBar = Config::oilMapMin
                                 + t * (Config::oilMapMax - Config::oilMapMin);
                } else {
                    ed.oilTargetBar = Config::oilMapMin;
                }
            }
            g_ctrlOilLoop.tick();
        }
        // Tick order matters:
        //  1. Governor first — adjusts throttleDemand toward N2 target (may reduce it).
        //  2. DynamicIdle second — enforces the idle RPM floor on throttleDemand.
        //     Running DI after the governor ensures the floor is always the last
        //     word: when governor reduces throttle below the DI floor both controllers
        //     no longer fight each tick, and ThrottleSlew sees a stable target.
        // Final throttle protection runs after automation rules so a rule can
        // request throttle without bypassing limp or slew/sensor safeguards.
        if (hw.hasGovernor && hw.hasN2Rpm && mode == SysMode::RUNNING) g_ctrlGovernor.tick();
        if (hw.hasDynamicIdle && mode == SysMode::RUNNING) g_ctrlDynamicIdle.tick();
    }

    inline void applyThrottleProtection() {
        auto& hw   = HardwareConfig::instance();
        auto& ed   = EngineData::instance();
        auto  mode = ed.mode;
        if (mode != SysMode::RUNNING && mode != SysMode::STARTUP) return;

        if (ed.limpMode && mode == SysMode::RUNNING) {
            float cap = constrain(Config::limpMaxThrottlePct / 100.0f, 0.0f, 1.0f);
            if (ed.throttleDemand > cap) ed.throttleDemand = cap;
        }
        if (hw.hasThrottleSlew) g_ctrlThrottleSlew.tick();
        // Running fuel floor: the measured fuel-pump minimum-spin %. Below it the
        // ESC stalls -> no fuel -> flameout, so nothing may leave RUNNING fuel under
        // it. Applied LAST — after the governor, dynamic idle, rules AND the slew's
        // pullback — so even the overspeed/EGT pullback (whose own floor can be
        // lower) can't undercut it. If a limit is so severe that the fuel floor
        // still overheats/overspeeds, the hard safety shutdown handles it; the
        // governor must not stall the pump. Standard value is 0 (uncalibrated -> no
        // floor); the user measures the real minimum via the min-spin calibration.
        if (mode == SysMode::RUNNING && Config::fuelPumpMinPct > 0.0f) {
            float floor = constrain(Config::fuelPumpMinPct / 100.0f, 0.0f, 1.0f);
            if (ed.throttleDemand < floor) ed.throttleDemand = floor;
        }
    }

} // namespace Hardware
