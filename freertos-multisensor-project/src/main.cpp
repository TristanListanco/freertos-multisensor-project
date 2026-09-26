#include "stm32f1xx_hal.h"
#include "FreeRTOS.h" // FreeRTOS definitions and time conversion macros.
#include "task.h"     // Task creation and blocking-delay functions.
#include "semphr.h"   // Mutex that lets tasks share the UART.
#include "queue.h"    // Queues that carry readings from SensorTask to its consumers.
#include "dht22.h"
#include "ldr.h"
#include "ssd1306.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Scheduling parameters observed in lab step 18.
#define SENSOR_TASK_PRIORITY 2
#define SENSOR_TASK_PERIOD_MS 2000 // also the DHT22's minimum time between reads
#define MONITOR_TASK_PRIORITY 3
#define MONITOR_TASK_PERIOD_MS 500

// Continuously running processing task (lab step 19).
#define PROCESSING_TASK_PRIORITY 2
#define PROCESSING_BATCH_SIZE 5000   // loop iterations per batch: finite work per pass
#define PROCESSING_TASK_BLOCK_MS 200 // blocked time after every batch
#define PROCESSING_LOG_EVERY 5       // log one batch in five, about once a second

// Consumers of SensorTask's readings (lab step 25).
#define DISPLAY_TASK_PRIORITY 1
#define ALARM_TASK_PRIORITY 3 // above SensorTask: alarms are handled as soon as a reading arrives
#define SENSOR_QUEUE_LENGTH 4 // readings a consumer can fall behind by before new ones are dropped

// One set of readings, passed from SensorTask to each consumer (lab step 24).
struct SensorData
{
    float temperature;   // degrees C; NAN if the DHT22 read failed
    float humidity;      // % relative humidity; NAN if the DHT22 read failed
    int lightLevel;      // 0-100 %, relative, not lux (see ldr.c); -1 if the read failed
    bool motionDetected; // always false: no motion sensor is wired yet
};

UART_HandleTypeDef huart1;
static SemaphoreHandle_t uartMutex;
static TaskHandle_t sensorTaskHandle;

// Each consumer gets its own queue. A FreeRTOS queue hands every item to exactly
// one receiver, so if DisplayTask and AlarmTask shared one, each would see only
// some of the readings.
static QueueHandle_t displayQueue;
static QueueHandle_t alarmQueue;

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

// Prints one line prefixed with the tick time. Several tasks share USART1; the
// mutex stops a task that preempts another mid-line from getting HAL_BUSY and
// losing its line.
static void logPrintf(const char *fmt, ...)
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

    xSemaphoreTake(uartMutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart1, (uint8_t *)line, len, 100);
    xSemaphoreGive(uartMutex);
}

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

// Reads every sensor once. Failures are logged here, where the reason is known;
// the reading carries only a "no value" marker.
static SensorData readSensors(void)
{
    SensorData data = {NAN, NAN, -1, false};

    int16_t temperature;
    uint16_t humidity;
    Dht22Status status = dht22Read(&temperature, &humidity);
    if (status == DHT22_OK)
    {
        data.temperature = temperature / 10.0f; // the DHT22 reports tenths
        data.humidity = humidity / 10.0f;
    }
    else
    {
        logPrintf("Sensor: DHT22 read failed (%s)", dht22StatusName(status));
    }

    uint16_t light;
    if (ldrRead(&light) == HAL_OK)
    {
        data.lightLevel = ldrLightPercent(light);
    }
    else
    {
        logPrintf("Sensor: LDR read failed");
    }

    return data;
}

// Copies a reading into a consumer's queue. The timeout is 0: if a consumer has
// fallen SENSOR_QUEUE_LENGTH readings behind, it misses this one rather than
// making SensorTask late for its next period.
static void sendReading(QueueHandle_t queue, const char *consumer, const SensorData &data)
{
    if (xQueueSend(queue, &data, 0) != pdPASS)
    {
        logPrintf("Sensor: %s queue full, reading dropped", consumer);
    }
}

// --- Sensor Task Definition ---
// vTaskDelayUntil counts each period from the previous wake time rather than
// from when readSensors finishes, so the reads start exactly
// SENSOR_TASK_PERIOD_MS apart however long they take.
void SensorTask(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        // RUNNING: this code only executes while SensorTask holds the CPU
        // (except for the 2-3 ms dht22Read blocks during its start signal).
        SensorData data = readSensors();
        sendReading(alarmQueue, "alarm", data);
        sendReading(displayQueue, "display", data);

        // BLOCKED: until the next period starts, SensorTask is off the CPU and
        // lower-priority tasks (Task A, Task B, Idle) run instead. When the
        // period is up, the tick interrupt makes it READY, and it resumes once
        // no higher-priority task is ready.
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
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

// Formats a value with 1 or 2 decimals, or "--" for NAN. newlib-nano's printf
// has no %f, so this prints the rounded value's digits as integers.
static const char *formatDecimal(char *buf, size_t size, float value, int decimals)
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

// Draws the room monitor screen (lab step 27). One decimal place, the DHT22's
// resolution.
static HAL_StatusTypeDef showOnOled(const SensorData &data)
{
    char number[16], value[20];
    snprintf(value, sizeof value, "%s C", formatDecimal(number, sizeof number, data.temperature, 1));

    ssd1306Clear();
    ssd1306DrawText(0, 0, "ROOM MONITOR", 1);
    ssd1306DrawText(0, 24, "Temperature", 1);
    ssd1306DrawText(0, 36, value, 2);
    return ssd1306Update();
}

// --- Display Task Definition ---
// First consumer, and the owner of the OLED: no other task initialises or draws
// on it, so the display driver needs no mutex (lab step 26). Each reading also
// goes to the serial port.
void DisplayTask(void *pvParameters)
{
    SensorData data = {NAN, NAN, -1, false};
    uint32_t received = 0;

    bool oledReady = ssd1306Init() == HAL_OK && showOnOled(data) == HAL_OK;
    if (!oledReady)
    {
        logPrintf("Display: OLED not responding, serial output only");
    }

    for (;;)
    {
        // Blocked until SensorTask sends a reading, so this never polls.
        xQueueReceive(displayQueue, &data, portMAX_DELAY);
        received++;

        char temperature[16], humidity[16], light[8] = "--";
        if (data.lightLevel >= 0)
        {
            snprintf(light, sizeof light, "%d", data.lightLevel);
        }
        logPrintf("Display #%lu: %s C, %s %%RH, light %s %%, motion %s", (unsigned long)received,
                  formatDecimal(temperature, sizeof temperature, data.temperature, 2),
                  formatDecimal(humidity, sizeof humidity, data.humidity, 2), light,
                  data.motionDetected ? "yes" : "no");

        if (oledReady && showOnOled(data) != HAL_OK)
        {
            logPrintf("Display: OLED update failed");
        }
    }
}

// --- Alarm Task Definition ---
// Second consumer. It has no alarm conditions yet; for now it shows that it
// receives every reading too: its count should match DisplayTask's.
void AlarmTask(void *pvParameters)
{
    SensorData data;
    uint32_t received = 0;

    for (;;)
    {
        xQueueReceive(alarmQueue, &data, portMAX_DELAY);
        logPrintf("Alarm #%lu: reading received, no alarm conditions yet", (unsigned long)++received);
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

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    // Crucial: Update internal clock variable so FreeRTOS calculates ticks correctly
    SystemCoreClockUpdate();

    MX_USART1_UART_Init();
    dht22Init();
    HAL_StatusTypeDef ldrStatus = ldrInit();

    const char *msg1 = "BCA182 FreeRTOS Multisensor\r\n";
    const char *msg2 = "System starting...\r\n";

    HAL_UART_Transmit(&huart1, (uint8_t *)msg1, strlen(msg1), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)msg2, strlen(msg2), 100);
    if (ldrStatus != HAL_OK)
    {
        const char *msg = "Warning: LDR ADC setup or calibration failed\r\n";
        HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    }

    uartMutex = xSemaphoreCreateMutex();
    displayQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData));
    alarmQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData));

    // Create Tasks and catch potential memory errors. 256-word stacks leave
    // room for the formatting in logPrintf.
    BaseType_t retA = xTaskCreate(TaskA, "TaskA", 256, NULL, 1, NULL);
    BaseType_t retB = xTaskCreate(TaskB, "TaskB", 256, NULL, 1, NULL);
    BaseType_t retS = xTaskCreate(SensorTask, "Sensor", 256, NULL, SENSOR_TASK_PRIORITY, &sensorTaskHandle);
    BaseType_t retM = xTaskCreate(StateMonitorTask, "Monitor", 256, NULL, MONITOR_TASK_PRIORITY, NULL);
    BaseType_t retP = xTaskCreate(ProcessingTask, "Process", 256, NULL, PROCESSING_TASK_PRIORITY, NULL);
    BaseType_t retD = xTaskCreate(DisplayTask, "Display", 256, NULL, DISPLAY_TASK_PRIORITY, NULL);
    BaseType_t retL = xTaskCreate(AlarmTask, "Alarm", 256, NULL, ALARM_TASK_PRIORITY, NULL);

    if (uartMutex == NULL || displayQueue == NULL || alarmQueue == NULL || retA != pdPASS ||
        retB != pdPASS || retS != pdPASS || retM != pdPASS || retP != pdPASS || retD != pdPASS ||
        retL != pdPASS)
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