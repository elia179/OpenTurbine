#include "EngineData.h"
#include <Arduino.h>
#include <string.h>

namespace {
portMUX_TYPE g_snapshotMux = portMUX_INITIALIZER_UNLOCKED;
alignas(EngineData) uint8_t g_publishedSnapshot[sizeof(EngineData)] = {};
uint32_t g_snapshotVersion = 0;
}

EngineData& EngineData::instance() {
    static EngineData inst;
    return inst;
}

void EngineData::publishSnapshot() {
    // Core 1 is the sole EngineData writer. Publishing once after the control
    // tick gives Core 0 an immutable, same-tick view without holding a lock
    // while JSON is generated.
    portENTER_CRITICAL(&g_snapshotMux);
    memcpy(g_publishedSnapshot, this, sizeof(EngineData));
    ++g_snapshotVersion;
    portEXIT_CRITICAL(&g_snapshotMux);
}

uint32_t EngineData::readPublishedSnapshot(void* destination, size_t destinationSize) {
    if (!destination || destinationSize < sizeof(EngineData)) return 0;
    portENTER_CRITICAL(&g_snapshotMux);
    memcpy(destination, g_publishedSnapshot, sizeof(EngineData));
    const uint32_t version = g_snapshotVersion;
    portEXIT_CRITICAL(&g_snapshotMux);
    return version;
}
