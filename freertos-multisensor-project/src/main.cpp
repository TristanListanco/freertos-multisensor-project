#include "stm32f1xx_hal.h"
#include "FreeRTOS.h" // FreeRTOS definitions and time conversion macros.
#include "task.h"     // Task creation and blocking-delay functions.
#include <string.h>

UART_HandleTypeDef huart1;

void SystemClock_Config(void);
static void MX_USART1_UART_Init(void);

extern "C" void xPortSysTickHandler(void);

// SysTick drives the HAL tick at all times, but only feeds FreeRTOS once the
// scheduler is running. Calling xPortSysTickHandler earlier makes the kernel
// touch a NULL pxCurrentTCB and pend a PendSV with no task to switch to.
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}

// --- Task A Definition ---
void TaskA(void *pvParameters)
{
    const char *msg = "Task A running\r\n";
    for (;;)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Task B Definition ---
void TaskB(void *pvParameters)
{
    const char *msg = "Task B running\r\n";
    // Offset slightly so Task A and Task B don't try to print at the exact same millisecond
    vTaskDelay(pdMS_TO_TICKS(500));
    for (;;)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    // Crucial: Update internal clock variable so FreeRTOS calculates ticks correctly
    SystemCoreClockUpdate();

    MX_USART1_UART_Init();

    const char *msg1 = "BCA182 FreeRTOS Multisensor\r\n";
    const char *msg2 = "System starting...\r\n";

    HAL_UART_Transmit(&huart1, (uint8_t *)msg1, strlen(msg1), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)msg2, strlen(msg2), 100);

    // Create Tasks and catch potential memory errors
    BaseType_t retA = xTaskCreate(TaskA, "TaskA", 128, NULL, 1, NULL);
    BaseType_t retB = xTaskCreate(TaskB, "TaskB", 128, NULL, 1, NULL);

    if (retA != pdPASS || retB != pdPASS)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)"Task creation failed\r\n", 22, 100);
        while (1)
            ;
    }

    // Hand over control to FreeRTOS
    vTaskStartScheduler();

    // If memory runs out before the scheduler starts, it falls down here
    HAL_UART_Transmit(&huart1, (uint8_t *)"Scheduler failed to start!\r\n", 28, 100);
    while (1)
    {
    }
}

// --- Minimal System Clock Configuration defaulting to HSI ---
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}

// --- USART1 Initialization ---
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

// --- USART1 Hardware Pins ---
extern "C" void HAL_UART_MspInit(UART_HandleTypeDef *uartHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (uartHandle->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        // PA9  ------> USART1_TX
        // PA10 ------> USART1_RX
        GPIO_InitStruct.Pin = GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}