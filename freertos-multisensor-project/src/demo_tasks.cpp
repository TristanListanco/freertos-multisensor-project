#include "demo_tasks.h"
#include "log.h"
#include "rtos_objects.h"

// Scheduling parameters observed in lab step 18.
#define MONITOR_TASK_PERIOD_MS 500

// Continuously running processing task (lab step 19).
#define PROCESSING_BATCH_SIZE 5000   // loop iterations per batch: finite work per pass
#define PROCESSING_TASK_BLOCK_MS 200 // blocked time after every batch
#define PROCESSING_LOG_EVERY 5       // log one batch in five, about once a second

static const char *stateName(eTaskState state)
{
    switch (state)
    {
    case eRunning:
        return "Running";
    case eReady:
        return "Ready";
    case eBlocked:
        return "Blocked";
    case eSuspended:
        return "Suspended";
    default:
        return "Deleted";
    }
}

// --- Task A Definition ---
void TaskA(void *pvParameters)
{
    for (;;)
    {
        logPrintf("Task A running");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Task B Definition ---
void TaskB(void *pvParameters)
{
    // Offset slightly so Task A and Task B don't try to print at the exact same millisecond
    vTaskDelay(pdMS_TO_TICKS(500));
    for (;;)
    {
        logPrintf("Task B running");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

// --- State Monitor Task Definition ---
// Samples SensorTask's state. It can never see Running: with one CPU, while
// this task runs, SensorTask does not. It normally sees Blocked: SensorTask's
// period starts a few ticks after this task's, once this task's first log line
// has been sent, so SensorTask never wakes on the same tick as a sample.
void StateMonitorTask(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        logPrintf("Monitor: SensorTask is %s", stateName(eTaskGetState(sensorTaskHandle)));
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(MONITOR_TASK_PERIOD_MS));
    }
}

// Placeholder for continuous data processing (e.g. filtering sensor samples)
// until real sensors are wired up. The loop bound is fixed, so each call does
// a finite amount of work.
static uint32_t processBatch(uint32_t state)
{
    for (uint32_t i = 0; i < PROCESSING_BATCH_SIZE; i++)
    {
        state = state * 1664525u + 1013904223u;
    }
    return state;
}

// --- Processing Task Definition ---
// Its work never runs out, so a bare for (;;) { processBatch(); } would keep it
// Ready forever and, at priority 2, starve Task A, Task B and Idle. Instead each
// pass does one bounded batch and then blocks.
void ProcessingTask(void *pvParameters)
{
    uint32_t state = 1;
    uint32_t batches = 0;

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        state = processBatch(state);
        TickType_t elapsed = xTaskGetTickCount() - start;

        if (++batches % PROCESSING_LOG_EVERY == 0)
        {
            logPrintf("Processing: batch #%lu took %lu ms, result %08lx",
                      (unsigned long)batches, (unsigned long)(elapsed * portTICK_PERIOD_MS),
                      (unsigned long)state);
        }

        // vTaskDelay, not vTaskDelayUntil: it always blocks, even after a batch
        // that overran the period. taskYIELD() would not be enough either: it
        // only hands the CPU to other ready priority-2 tasks, so the
        // lower-priority tasks would still never run.
        vTaskDelay(pdMS_TO_TICKS(PROCESSING_TASK_BLOCK_MS));
    }
}
