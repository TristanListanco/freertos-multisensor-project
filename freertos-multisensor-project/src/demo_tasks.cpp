#include "demo_tasks.h"
#include "log.h"
#include "rtos_objects.h"

// Continuously running processing task (lab step 19).
#define PROCESSING_BATCH_SIZE 5000   // loop iterations per batch: finite work per pass
#define PROCESSING_TASK_BLOCK_MS 200 // blocked time after every batch
#define PROCESSING_LOG_EVERY 5       // log one batch in five, about once a second

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
// Ready forever and, at priority 2, starve DisplayTask and Idle. Instead each
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
