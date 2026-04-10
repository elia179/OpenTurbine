#include "EngineData.h"

EngineData& EngineData::instance() {
    static EngineData inst;
    return inst;
}
