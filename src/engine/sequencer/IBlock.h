#pragma once
#include "../EngineData.h"
#include <stdio.h>   // snprintf
#include <string.h>  // strncpy

// ── Sequence block result ─────────────────────────────────────
enum class BlockResult {
    Running,    // still executing — call tick() next loop
    Complete,   // done cleanly — advance to next block
    Abort,      // abort to STANDBY with no shutdown (engine never ran)
    Fault       // trigger full shutdown sequence
};

// ── Block interface ───────────────────────────────────────────
// Every startup and shutdown block implements this.
// onEnter() / onExit() are optional; tick() is mandatory.
// SequenceEngine calls these — blocks never call each other.
class IBlock {
public:
    virtual ~IBlock() = default;
    virtual const char* name()      = 0;
    virtual void        onEnter()   {}
    virtual BlockResult tick()      = 0;
    virtual void        onExit()    {}

protected:
    static void setWaitReason(const char* reason) {
        strncpy(EngineData::instance().seqWaitReason, reason, sizeof(EngineData::instance().seqWaitReason) - 1);
        EngineData::instance().seqWaitReason[sizeof(EngineData::instance().seqWaitReason) - 1] = '\0';
    }
    static void clearWaitReason() {
        EngineData::instance().seqWaitReason[0] = '\0';
    }
};
