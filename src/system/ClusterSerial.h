#pragma once
#include "../engine/EngineData.h"
#include "../engine/Types.h"
#include <HardwareSerial.h>
#include <stdint.h>

#ifndef OT_CLUSTER_BAUD
#define OT_CLUSTER_BAUD 115200UL
#endif

// ============================================================
//  ClusterSerial - external display / device link
//
//  OTC binary is a small framed protocol for custom clusters:
//    magic "OT", version, type, sequence, payload length, payload, CRC16.
//    It sends a boot hello, schema field descriptions, limits, status/events,
//    and compact live telemetry in canonical ECU units.
//
//  Optional RX:
//    If hardware.cluster_serial.rx_pin is fitted, a cluster can send simple
//    newline commands:
//      OTC:PING
//      OTC:SCHEMA?
//      OTC:SUB,DEFAULT
//      OTC:SUB,ALL
//      OTC:SUB,N1_RPM,TOT_C,OIL_BAR
//      OTC:CMD,STOP
//      OTC:CMD,START
//      OTC:CMD,AB_STOP
//      OTC:CMD,RESET_PEAKS
//      OTC:CMD,LIMP_TOGGLE
//      OTC:CMD,DYNAMIC_IDLE_TOGGLE
//
//    Engine command safety is still enforced by the ordinary command handler.
// ============================================================

namespace ClCode {
    static constexpr uint8_t Running          =  1;
    static constexpr uint8_t RelightActive    =  2;
    static constexpr uint8_t StartingUp       =  3;
    static constexpr uint8_t ReadyToStart     =  4;
    static constexpr uint8_t Igniting         =  5;
    static constexpr uint8_t Ignited          =  6;
    static constexpr uint8_t IgnitionFailed   =  7;
    static constexpr uint8_t WaitingN1Rise    =  8;
    static constexpr uint8_t FlameOut         =  9;
    static constexpr uint8_t ShuttingDown     = 10;
    static constexpr uint8_t CooldownRunning  = 11;
    static constexpr uint8_t OilCalFailed     = 12;
    static constexpr uint8_t StopSwitchActive = 13;
    static constexpr uint8_t OilPressureLow   = 14;
    static constexpr uint8_t Overspeed        = 15;
    static constexpr uint8_t OilZero          = 16;
    static constexpr uint8_t TotHigh          = 17;
    static constexpr uint8_t OilWarn          = 18;
}

class ClusterSerial {
public:
    static void begin();
    static void tick();
    static void sendStatus(uint8_t code);
    static void sendEvent(uint8_t severity, const char* text);

private:
    static unsigned long _lastDataMs;
    static SysMode       _lastMode;
    static uint8_t       _lastClusterCode;
    static uint8_t       _lastStatusCode;
    static bool          _totWarnActive;
    static bool          _oilWarnActive;
    static bool          _schemaDirty;
    static uint8_t       _seq;
    static char          _rxLine[80];
    static uint8_t       _rxLen;
    static unsigned long _lastSchemaMs;
    static unsigned long _nextSchemaMs;

    static void _sendSchema();
    static void _sendTelemetry();
    static void _pollRx();
    static void _handleLine(const char* line);
};
