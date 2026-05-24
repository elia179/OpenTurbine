#pragma once
// ============================================================
//  RulesEngine — simple "if sensor op threshold then actuator"
//  automation rules, evaluated every control tick.
//
//  Rules are defined on the Config page ("Automation Rules"
//  section) and stored in config JSON under "rules": [...].
//
//  Rules run AFTER the sequencer and controller writes, so
//  they can augment but not override safety shutdowns.
//  Evaluation is suppressed in STANDBY (engine off).
// ============================================================
#include "Config.h"
#include "../engine/EngineData.h"

class RulesEngine {
public:
    // Sensor indices (match UI dropdown order)
    enum Sensor   : uint8_t { OIL_TEMP=0, TOT=1, N1_RPM=2, OIL_PRESS=3, TIT=4, BATT_V=5, N2_RPM=6,
                              DI_CH0=7, DI_CH1=8, DI_CH2=9, DI_CH3=10 };
    // Comparison operators
    enum Op       : uint8_t { GT=0, LT=1, GTE=2, LTE=3, EQ=4 };
    // Controllable actuators
    enum Actuator : uint8_t { COOL_FAN=0, BLEED_VALVE=1, FUEL_PUMP2=2 };

    // Called once per control tick (Core 1, ~10 ms cycle)
    static void evaluate() {
        auto& ed = EngineData::instance();
        // Only evaluate when engine is doing something real
        if (ed.mode == SysMode::STANDBY || ed.benchMode) return;

        for (int i = 0; i < Config::ruleCount; i++) {
            const Config::Rule& r = Config::rules[i];
            if (!r.enabled) continue;

            float sval = _readSensor(r.sensor, ed);
            bool  met  = _evalOp(sval, r.op, r.threshold);
            float dem  = met ? r.onValue : r.offValue;

            _applyActuator(r.actuator, dem, ed);
        }
    }

private:
    static float _readSensor(uint8_t s, const EngineData& ed) {
        switch (s) {
            case OIL_TEMP:  return ed.oilTemp;
            case TOT:       return ed.tot;
            case N1_RPM:    return ed.n1Rpm;
            case OIL_PRESS: return ed.oilPressure;
            case TIT:       return ed.tit;
            case BATT_V:    return ed.battVoltage;
            case N2_RPM:    return ed.n2Rpm;
            case DI_CH0:    return ed.diState[0] ? 1.0f : 0.0f;
            case DI_CH1:    return ed.diState[1] ? 1.0f : 0.0f;
            case DI_CH2:    return ed.diState[2] ? 1.0f : 0.0f;
            case DI_CH3:    return ed.diState[3] ? 1.0f : 0.0f;
            default:        return 0.0f;
        }
    }

    static bool _evalOp(float val, uint8_t op, float threshold) {
        switch (op) {
            case GT:  return val >  threshold;
            case LT:  return val <  threshold;
            case GTE: return val >= threshold;
            case LTE: return val <= threshold;
            case EQ:  return fabsf(val - threshold) < 1.0f;
            default:  return false;
        }
    }

    static void _applyActuator(uint8_t act, float dem, EngineData& ed) {
        switch (act) {
            case COOL_FAN:    ed.coolFanOn      = (dem >= 0.5f); break;
            case BLEED_VALVE: ed.bleedValveOpen = (dem >= 0.5f); break;
            case FUEL_PUMP2:  ed.fuelPump2Demand = constrain(dem, 0.0f, 1.0f); break;
            default: break;
        }
    }
};
