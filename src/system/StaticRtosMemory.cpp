#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

// The framework's default vApplicationGetTimerTaskMemory() calls pvPortMalloc
// during early startup. On the memory-constrained classic ESP32, unrelated
// early allocations can fragment DRAM enough that its mandatory 4 KiB stack
// request returns null before setup() runs. FreeRTOS explicitly supports an
// application-owned static buffer for this task; use it on both targets so
// kernel startup does not depend on heap shape.
extern "C" void __wrap_vApplicationGetTimerTaskMemory(
    StaticTask_t** taskTcb,
    StackType_t** taskStack,
    uint32_t* stackSize) {
    static StaticTask_t timerTcb;
    static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];
    *taskTcb = &timerTcb;
    *taskStack = timerStack;
    *stackSize = configTIMER_TASK_STACK_DEPTH;
}
