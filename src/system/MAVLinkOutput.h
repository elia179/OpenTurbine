#pragma once
#include "../engine/EngineData.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#include <HardwareSerial.h>

// ============================================================
//  MAVLinkOutput — minimal MAVLink v1 UART telemetry
//
//  Sends three MAVLink message types over UART2 (configurable):
//
//  1. HEARTBEAT (id 0)     — system/autopilot type, mode, status
//  2. NAMED_VALUE_FLOAT (id 251) — engine sensor values by name
//  3. STATUSTEXT (id 253)  — fault description / last event text
//
//  No external library required — messages hand-crafted per the
//  MAVLink v1 wire format (start, len, seq, sys, comp, msgid,
//  payload, crc16-X25).
//
//  Typical use: connect a ground station (Mission Planner,
//  QGroundControl, MAVProxy) to the UART TX pin.  Engine data
//  appears as named floats visible in the "Status" tab.
//
//  Enable: set HardwareConfig::hasMAVLink = true and configure
//  mavlinkTxPin / mavlinkBaud / mavlinkIntervalMs.
//
//  Note: this is a TX-only implementation — no RX / command
//  handling.  The MAVLink system ID is 1 (component 200).
// ============================================================

class MAVLinkOutput {
public:
    static constexpr uint8_t SYS_ID   = 1;
    static constexpr uint8_t COMP_ID  = 200;  // onboard computer
    static constexpr uint8_t MAV_TYPE_GENERIC          = 0;
    static constexpr uint8_t MAV_AUTOPILOT_INVALID     = 8;
    static constexpr uint8_t MAV_STATE_ACTIVE          = 4;
    static constexpr uint8_t MAV_STATE_STANDBY         = 3;
    static constexpr uint8_t MAV_STATE_EMERGENCY       = 6;

    void begin(HardwareSerial& serial) {
        _serial = &serial;
        _seq    = 0;
    }

    // Call each ECU loop tick — internally rate-limited
    void tick() {
        if (!_serial) return;
        unsigned long now = millis();
        auto& hw = HardwareConfig::instance();
        if ((now - _lastMs) < (unsigned long)hw.mavlinkIntervalMs) return;
        _lastMs = now;

        _sendHeartbeat();
        _sendEngineData();
    }

    // Call when a fault/event occurs — sends a STATUSTEXT message immediately
    void sendStatusText(const char* text) {
        if (!_serial) return;
        _sendStatusText(text);
    }

private:
    HardwareSerial* _serial = nullptr;
    uint8_t  _seq           = 0;
    unsigned long _lastMs   = 0;

    // ── CRC-16/MCRF4XX (X25) ─────────────────────────────────
    static uint16_t _crc16(const uint8_t* buf, size_t len, uint8_t extra) {
        uint16_t crc = 0xFFFF;
        while (len--) {
            uint8_t b = *buf++;
            crc ^= (uint16_t)b;
            for (int i = 0; i < 8; i++) {
                if (crc & 1) crc = (crc >> 1) ^ 0x8408;
                else         crc >>= 1;
            }
        }
        // Extra byte (CRC_EXTRA from message definition)
        crc ^= (uint16_t)extra;
        for (int i = 0; i < 8; i++) {
            if (crc & 1) crc = (crc >> 1) ^ 0x8408;
            else         crc >>= 1;
        }
        return crc;
    }

    void _sendPacket(uint8_t msgId, uint8_t crcExtra,
                     const uint8_t* payload, uint8_t payLen) {
        uint8_t hdr[6] = {
            0xFE,    // magic
            payLen,  // payload length
            _seq++,  // sequence
            SYS_ID,
            COMP_ID,
            msgId
        };
        // CRC covers header bytes 1..5, then payload, then crcExtra (MAVLink v1 spec).
        // Process inline — do not call _crc16() with extra=0, because _crc16 always
        // runs the extra byte through the polynomial and 0x00 is not a no-op.
        uint16_t crc = 0xFFFF;
        for (int i = 1; i <= 5; i++) {
            crc ^= (uint16_t)hdr[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) crc = (crc >> 1) ^ 0x8408;
                else         crc >>= 1;
            }
        }
        for (uint8_t i = 0; i < payLen; i++) {
            uint8_t b = payload[i];
            crc ^= b;
            for (int j = 0; j < 8; j++) {
                if (crc & 1) crc = (crc >> 1) ^ 0x8408;
                else         crc >>= 1;
            }
        }
        crc ^= (uint16_t)crcExtra;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0x8408;
            else         crc >>= 1;
        }

        _serial->write(hdr, 6);
        _serial->write(payload, payLen);
        _serial->write((uint8_t)(crc & 0xFF));
        _serial->write((uint8_t)(crc >> 8));
    }

    void _sendHeartbeat() {
        // HEARTBEAT: msg_id=0, CRC_EXTRA=50, payload=9 bytes
        // custom_mode(4), type(1), autopilot(1), base_mode(1), system_status(1), mavlink_version(1)
        const auto& ed = EngineData::instance();
        uint8_t sysStatus = MAV_STATE_STANDBY;
        if (ed.mode == SysMode::RUNNING || ed.mode == SysMode::STARTUP) sysStatus = MAV_STATE_ACTIVE;
        if (ed.mode == SysMode::FAULT)   sysStatus = MAV_STATE_EMERGENCY;

        uint8_t payload[9] = {};
        // custom_mode = (uint32_t)mode — little endian
        uint32_t cm = (uint32_t)ed.mode;
        payload[0] = cm & 0xFF; payload[1] = (cm>>8)&0xFF;
        payload[2] = (cm>>16)&0xFF; payload[3] = (cm>>24)&0xFF;
        payload[4] = MAV_TYPE_GENERIC;
        payload[5] = MAV_AUTOPILOT_INVALID;
        payload[6] = 0;              // base_mode
        payload[7] = sysStatus;
        payload[8] = 3;              // mavlink_version
        _sendPacket(0, 50, payload, 9);
    }

    // Send one NAMED_VALUE_FLOAT message (msg_id=251, CRC_EXTRA=170)
    // payload: time_boot_ms(4) + value(4) + name(10) = 18 bytes
    void _sendNamedFloat(const char* name, float value) {
        uint8_t payload[18] = {};
        uint32_t t = millis();
        payload[0] = t & 0xFF; payload[1] = (t>>8)&0xFF;
        payload[2] = (t>>16)&0xFF; payload[3] = (t>>24)&0xFF;
        // value at offset 4 (little-endian IEEE754)
        memcpy(payload + 4, &value, 4);
        // name: 10 bytes, null-padded
        strncpy((char*)(payload + 8), name, 10);
        _sendPacket(251, 170, payload, 18);
    }

    void _sendEngineData() {
        const auto& ed = EngineData::instance();
        _sendNamedFloat("N1_RPM",   ed.n1Rpm);
        _sendNamedFloat("TOT_C",    ed.tot);
        _sendNamedFloat("OIL_BAR",  ed.oilPressure);
        if (ed.n2Healthy)        _sendNamedFloat("N2_RPM",   ed.n2Rpm);
        if (ed.oilTempHealthy)   _sendNamedFloat("OIL_T_C",  ed.oilTemp);
        if (ed.battHealthy)      _sendNamedFloat("BATT_V",   ed.battVoltage);
        if (ed.fuelPressHealthy) _sendNamedFloat("FUEL_BAR", ed.fuelPressure);
        if (ed.torqueHealthy)    _sendNamedFloat("TORQ_NM",  ed.torque);
        if (ed.titHealthy)       _sendNamedFloat("TIT_C",    ed.tit);
        if (HardwareConfig::hasFuelFlow) _sendNamedFloat("FUEL_KGH", ed.fuelFlow);
        _sendNamedFloat("THR_PCT",  ed.throttleDemand * 100.0f);
    }

    void _sendStatusText(const char* text) {
        // STATUSTEXT: msg_id=253, CRC_EXTRA=83, payload=51 bytes
        // severity(1) + text(50)
        uint8_t payload[51] = {};
        payload[0] = 3; // MAV_SEVERITY_ERROR
        strncpy((char*)(payload + 1), text, 50);
        _sendPacket(253, 83, payload, 51);
    }
};
