#pragma once
#include "../IBlock.h"
#include "../../EngineData.h"
#include "../../../system/HardwareConfig.h"
#include <Arduino.h>

// ============================================================
//  WaitForInput — holds sequence until a general-purpose DI
//  channel reaches the expected state.
//
//  channelIdx   : index into HardwareConfig::diCh[] (0–3)
//  expectedState: true = wait until active, false = wait until inactive
//  timeoutMs    : max wait (0 = wait forever); on timeout returns Abort
// ============================================================
class WaitForInput : public IBlock {
public:
    int           channelIdx    = 0;
    bool          expectedState = true;
    unsigned long timeoutMs     = 0;

    const char* name() override { return "WaitForInput"; }

    void onEnter() override {
        _entryMs = millis();
        auto& hw = HardwareConfig::instance();
        const char* lbl = hw.diCh[channelIdx].label[0]
                          ? hw.diCh[channelIdx].label
                          : "DI";
        Serial.printf("[WaitForInput] Waiting for ch%d (%s) to be %s\n",
                      channelIdx, lbl, expectedState ? "active" : "inactive");
    }

    BlockResult tick() override {
        auto& ed = EngineData::instance();
        bool state = (channelIdx >= 0 && channelIdx < HardwareConfig::MAX_DI)
                     ? ed.diState[channelIdx] : false;

        if (state == expectedState) {
            Serial.printf("[WaitForInput] ch%d condition met\n", channelIdx);
            return BlockResult::Complete;
        }
        if (timeoutMs > 0 && (millis() - _entryMs) >= timeoutMs) {
            Serial.printf("[WaitForInput] ch%d timeout after %lums\n", channelIdx, timeoutMs);
            return BlockResult::Abort;
        }
        return BlockResult::Running;
    }

private:
    unsigned long _entryMs = 0;
};
