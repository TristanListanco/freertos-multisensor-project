#include "motion.h"
#include "log.h"
#include "rtos_objects.h"
#include "stm32f1xx_hal.h"
#include "system_state.h"

#define MOTION_POLL_MS 100          // the PIR holds its output high for seconds, so 10 Hz is plenty
#define INACTIVITY_TIMEOUT_MS 15000 // short, for laboratory testing
#define PIR_PORT GPIOA
#define PIR_PIN GPIO_PIN_3

void motionInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // Pull-down: a disconnected sensor reads as "no motion".
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PIR_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(PIR_PORT, &GPIO_InitStruct);
}

// The PIR module drives OUT high while it senses motion, and for its hold time
// (a few seconds) after. Only MotionTask calls this; others use EVENT_MOTION.
static bool pirMotionDetected(void)
{
    return HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN) == GPIO_PIN_SET;
}

bool systemIsActive(void)
{
    return (xEventGroupGetBits(systemEvents) & EVENT_ACTIVE) != 0;
}

// Updates EVENT_ACTIVE and tells DisplayTask, in that order, so SensorTask and
// InputTask see the new state before the OLED changes.
static void publishSystemState(SystemState state)
{
    if (state == SystemState::ACTIVE)
    {
        xEventGroupSetBits(systemEvents, EVENT_ACTIVE);
    }
    else
    {
        xEventGroupClearBits(systemEvents, EVENT_ACTIVE);
    }
    xQueueOverwrite(systemStateQueue, &state);
}

// --- Motion Task Definition ---
// Owns the system state (lab steps 31-32). Polls the PIR every MOTION_POLL_MS
// with vTaskDelayUntil and feeds evaluateSystemState in lib/system_state, which
// decides the transitions and is unit tested on the host. It runs in both
// states: motion detection stays operational while INACTIVE (lab step 34).
void MotionTask(void *pvParameters)
{
    SystemState state = SystemState::ACTIVE;
    TickType_t lastMotion = xTaskGetTickCount(); // start ACTIVE, with the full timeout
    TickType_t lastWakeTime = lastMotion;

    publishSystemState(state);
    logPrintf("Motion: %s, timeout %d s", systemStateName(state), INACTIVITY_TIMEOUT_MS / 1000);

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        bool motion = pirMotionDetected();
        if (motion)
        {
            lastMotion = now;
            xEventGroupSetBits(systemEvents, EVENT_MOTION);
        }
        else
        {
            xEventGroupClearBits(systemEvents, EVENT_MOTION);
        }

        SystemState next = evaluateSystemState(state, motion, (now - lastMotion) * portTICK_PERIOD_MS,
                                               INACTIVITY_TIMEOUT_MS);
        if (next != state)
        {
            state = next;
            publishSystemState(state);
            logPrintf("Motion: %s (%s)", systemStateName(state),
                      motion ? "motion detected" : "inactivity timeout");
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(MOTION_POLL_MS));
    }
}
