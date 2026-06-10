#pragma once
#include "IBlock.h"
#include "../EngineData.h"
#include "../../system/FlightRecorder.h"
#include "../../system/HardwareConfig.h"
#include "../../system/RulesEngine.h"
#include <stddef.h>
#include <stdio.h>   // snprintf

// ============================================================
//  SequenceEngine — runs an ordered array of IBlock*
//
//  One active sequence at a time (startup OR shutdown).
//  Called by Hardware::tick() via SequenceEngine::tick().
//
//  On each tick():
//    1. Call activeBlock->tick()
//    2. Route result: Complete → next, Abort → standby, Fault → shutdown
//    3. Call onExit() / onEnter() at transitions
//
//  No dynamic allocation — sequences are compiled-in pointer arrays.
// ============================================================

class SequenceEngine {
public:
    using DoneFn    = void(*)();   // called when sequence finishes normally
    using AbortFn   = void(*)();   // called on BlockResult::Abort
    using FaultFn   = void(*)();   // called on BlockResult::Fault

    void setCallbacks(DoneFn done, AbortFn abort, FaultFn fault) {
        _done  = done;
        _abort = abort;
        _fault = fault;
    }

    void startSequence(IBlock** blocks, size_t count,
                       const HardwareConfig::SeqSideAction (*enterActions)[HardwareConfig::MAX_SEQ_SIDE_ACTIONS] = nullptr,
                       const HardwareConfig::SeqSideAction (*exitActions)[HardwareConfig::MAX_SEQ_SIDE_ACTIONS] = nullptr) {
        // If a sequence is already running, exit the current block cleanly so its
        // onExit() cleanup runs (e.g. ABIgnite restores pre-torch throttle).
        // Without this, interrupting mid-sequence (e.g. fault during AB ignition)
        // leaves actuators in whatever state the block had set them.
        if (_running && _blocks && _idx < _count) {
            _blocks[_idx]->onExit();
        }
        _blocks  = blocks;
        _count   = count;
        _idx     = 0;
        _enterActions = enterActions;
        _exitActions  = exitActions;
        _running = count > 0;
        auto& ed = EngineData::instance();
        ed.seqBlockTotal = (uint8_t)count;
        ed.seqBlockIdx   = 0;
        if (_running) _enter(0);
    }

    void stopSequence() {
        if (_running && _idx < _count) {
            _blocks[_idx]->onExit();
        }
        _running = false;
        _blocks  = nullptr;
        _count   = 0;
        _idx     = 0;
        _enterActions = nullptr;
        _exitActions  = nullptr;
        auto& ed = EngineData::instance();
        ed.currentBlock[0] = '\0';
        ed.seqBlockTotal   = 0;
        ed.seqBlockIdx     = 0;
    }

    void tick() {
        if (!_running || !_blocks || _idx >= _count) return;

        BlockResult r = _blocks[_idx]->tick();

        switch (r) {
            case BlockResult::Running:
                break;

            case BlockResult::Complete:
                FlightRecorder::logBlockExit(_blocks[_idx]->name(), "ok");
                _blocks[_idx]->onExit();
                _applyActions(_exitActions, _idx);
                _idx++;
                if (_idx >= _count) {
                    _running = false;
                    if (_done) _done();
                } else {
                    _enter(_idx);
                }
                break;

            case BlockResult::Abort:
                // In bench mode: treat abort as Complete so the full sequence still runs.
                // Real engines need the abort path; bench tests just need to step through.
                if (EngineData::instance().benchMode) {
                    FlightRecorder::logBlockExit(_blocks[_idx]->name(), "bench_mode_skip");
                    _blocks[_idx]->onExit();
                    _applyActions(_exitActions, _idx);
                    _idx++;
                    if (_idx >= _count) { _running = false; if (_done) _done(); }
                    else { _enter(_idx); }
                    break;
                }
                FlightRecorder::logBlockExit(_blocks[_idx]->name(), "abort");
                _blocks[_idx]->onExit();
                _running = false;
                if (_abort) _abort();
                break;

            case BlockResult::Fault:
                // Same: bench mode converts fault to Continue rather than shutdown.
                if (EngineData::instance().benchMode) {
                    FlightRecorder::logBlockExit(_blocks[_idx]->name(), "bench_mode_skip");
                    _blocks[_idx]->onExit();
                    _applyActions(_exitActions, _idx);
                    _idx++;
                    if (_idx >= _count) { _running = false; if (_done) _done(); }
                    else { _enter(_idx); }
                    break;
                }
                FlightRecorder::logBlockExit(_blocks[_idx]->name(), "fault");
                _blocks[_idx]->onExit();
                _running = false;
                if (_fault) _fault();
                break;
        }
    }

    bool isRunning()         const { return _running; }
    int  currentBlockIndex() const { return (int)_idx; }
    const char* currentBlockName() const {
        if (_running && _blocks && _idx < _count) return _blocks[_idx]->name();
        return "IDLE";
    }

private:
    IBlock**  _blocks  = nullptr;
    size_t    _count   = 0;
    size_t    _idx     = 0;
    const HardwareConfig::SeqSideAction (*_enterActions)[HardwareConfig::MAX_SEQ_SIDE_ACTIONS] = nullptr;
    const HardwareConfig::SeqSideAction (*_exitActions)[HardwareConfig::MAX_SEQ_SIDE_ACTIONS] = nullptr;
    bool      _running = false;
    DoneFn    _done    = nullptr;
    AbortFn   _abort   = nullptr;
    FaultFn   _fault   = nullptr;

    void _enter(size_t i) {
        const char* bname = _blocks[i]->name();
        auto& ed = EngineData::instance();
        // Update last-event for dashboard display
        snprintf(ed.lastEvent, sizeof(ed.lastEvent), "Seq: %s", bname);
        // Update sequence progress fields for web UI
        strncpy(ed.currentBlock, bname, sizeof(ed.currentBlock) - 1);
        ed.currentBlock[sizeof(ed.currentBlock) - 1] = '\0';
        ed.seqBlockIdx = (uint8_t)i;
        FlightRecorder::logBlockEnter(bname);
        _blocks[i]->onEnter();
        _applyActions(_enterActions, i);
    }

    void _applyActions(const HardwareConfig::SeqSideAction (*actions)[HardwareConfig::MAX_SEQ_SIDE_ACTIONS], size_t i) {
        if (!actions || i >= HardwareConfig::MAX_SEQ_BLOCKS) return;
        for (int j = 0; j < HardwareConfig::MAX_SEQ_SIDE_ACTIONS; j++) {
            const auto& a = actions[i][j];
            if (!a.enabled) continue;
            RulesEngine::applyActuatorDemand(a.actuator, a.value);
        }
    }
};
