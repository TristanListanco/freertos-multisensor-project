#include "log.h"
#include "rtos_objects.h"
#include "stm32f1xx_hal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef huart1;

void logInit(void)
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
// Overrides HAL's weak HAL_UART_MspInit, so the signature must match HAL's
// exactly, including the non-const pointer.
// cppcheck-suppress constParameterPointer
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

void logWriteDirect(const char *text)
{
    HAL_UART_Transmit(&huart1, reinterpret_cast<const uint8_t *>(text), strlen(text), 100);
}

// serialMutex makes each line one uninterrupted transmission. Without it, a
// task that preempted another mid-line would find the UART busy and its line
// would be lost or mixed into the other one. It is a mutex rather than a binary
// semaphore for priority inheritance: while a high-priority task (InputTask)
// waits for a line from a low-priority one (DisplayTask), the holder runs at
// the waiter's priority, so mid-priority tasks (SensorTask, ProcessingTask)
// can't stretch the wait. The line is formatted before taking the mutex, so
// the mutex is held only while the bytes go out (up to about 7 ms for a full
// line at 115200 baud).
void logPrintf(const char *fmt, ...)
{
    char line[80];
    size_t len = snprintf(line, sizeof line, "[%6lu ms] ",
                          (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));

    va_list args;
    va_start(args, fmt);
    vsnprintf(line + len, sizeof line - len - 2, fmt, args); // leave room for "\r\n"
    va_end(args);

    len = strlen(line);
    line[len++] = '\r';
    line[len++] = '\n';

    xSemaphoreTake(serialMutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart1, reinterpret_cast<const uint8_t *>(line), len, 100);
    // The cast is inside FreeRTOS's xSemaphoreGive macro, not in this code.
    // cppcheck-suppress cstyleCast
    xSemaphoreGive(serialMutex);
}

const char *formatDecimal(char *buf, size_t size, float value, int decimals)
{
    if (isnan(value))
    {
        return "--";
    }
    long unit = decimals == 1 ? 10 : 100;
    long scaled = lroundf(value * unit);
    long magnitude = scaled < 0 ? -scaled : scaled;
    snprintf(buf, size, "%s%ld.%0*ld", scaled < 0 ? "-" : "", magnitude / unit, decimals,
             magnitude % unit);
    return buf;
}
