#include "input.h"
#include "display.h"
#include "log.h"
#include "motion.h"
#include "rtos_objects.h"
#include "stm32f1xx_hal.h"

#define ENCODER_PORT GPIOA
#define ENCODER_CLK_PIN GPIO_PIN_1 // EXTI1 interrupt on every falling edge
#define ENCODER_DT_PIN GPIO_PIN_2

void inputInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = ENCODER_CLK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENCODER_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ENCODER_DT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(ENCODER_PORT, &GPIO_InitStruct);

    // Lowest priority, the same as the kernel's own SysTick and PendSV.
    HAL_NVIC_SetPriority(EXTI1_IRQn, 15, 0);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

// --- Input Task Definition ---
// Owns the page selection. Blocked until the encoder ISR queues a step, then
// moves one page (nextDisplayMode/previousDisplayMode in lib/display_navigation,
// unit tested on the host) and hands the new page to DisplayTask, which owns
// the OLED.
// The encoder is active only while the system is ACTIVE (lab step 33): with the
// OLED off there is no page to change, and only the PIR wakes the system.
void InputTask(void *pvParameters)
{
    DisplayMode mode = DisplayMode::TEMPERATURE;
    int8_t step;

    for (;;)
    {
        xQueueReceive(encoderQueue, &step, portMAX_DELAY);
        if (!systemIsActive())
        {
            logPrintf("Input: ignored, system INACTIVE");
            continue;
        }
        mode = step > 0 ? nextDisplayMode(mode) : previousDisplayMode(mode);
        xQueueOverwrite(modeQueue, &mode);
        logPrintf("Input: %s, page %s", step > 0 ? "clockwise" : "counterclockwise", displayModeName(mode));
    }
}

// HAL calls this for every EXTI line; only the encoder uses EXTI so far.
// CLK falls once per detent. DT's level at that moment gives the direction:
// high for clockwise, low for counterclockwise.
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == ENCODER_CLK_PIN)
    {
        int8_t step = HAL_GPIO_ReadPin(ENCODER_PORT, ENCODER_DT_PIN) == GPIO_PIN_SET ? 1 : -1;
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(encoderQueue, &step, &higherPriorityTaskWoken); // dropped if the queue is full
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

extern "C" void EXTI1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(ENCODER_CLK_PIN);
}
