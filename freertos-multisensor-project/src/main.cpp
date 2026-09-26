#include "stm32f1xx_hal.h"
#include "FreeRTOS.h" // FreeRTOS definitions and time conversion macros.
#include "task.h"     // Task creation and blocking-delay functions.
#include "semphr.h"   // Mutex that lets tasks share the UART.
#include "queue.h"    // Queues that carry readings from SensorTask to its consumers.
#include "event_groups.h" // Broadcasts ACTIVE/INACTIVE to the tasks that pause.
#include "dht22.h"
#include "ldr.h"
#include "ssd1306.h"
#include "alarm_logic.h"
#include "system_state.h"
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
    bool motionDetected; // PIR output high when the reading was taken
};

// Motion and system state (lab steps 31-34).
#define MOTION_TASK_PRIORITY 3      // wakes the system; its work per poll is tiny
#define MOTION_POLL_MS 100          // the PIR holds its output high for seconds, so 10 Hz is plenty
#define INACTIVITY_TIMEOUT_MS 15000 // short, for laboratory testing
#define PIR_PORT GPIOA
#define PIR_PIN GPIO_PIN_3

// Rotary encoder navigation (lab steps 28-29).
#define INPUT_TASK_PRIORITY 3 // it only relays encoder steps, so running at once costs almost nothing
#define ENCODER_PORT GPIOA
#define ENCODER_CLK_PIN GPIO_PIN_1 // EXTI1 interrupt on every falling edge
#define ENCODER_DT_PIN GPIO_PIN_2
#define ENCODER_QUEUE_LENGTH 8 // steps the ISR can queue before InputTask catches up

// The page the OLED shows (lab step 28). Clockwise moves down this list,
// counterclockwise up, wrapping around at both ends (lab step 29).
enum class DisplayMode
{
    TEMPERATURE,
    HUMIDITY,
    LIGHT,
    MOTION
};
constexpr int DISPLAY_MODE_COUNT = static_cast<int>(DisplayMode::MOTION) + 1;

UART_HandleTypeDef huart1;
static SemaphoreHandle_t serialMutex; // guards USART1; see logPrintf
static TaskHandle_t sensorTaskHandle;

// Each consumer gets its own queue. A FreeRTOS queue hands every item to exactly
// one receiver, so if DisplayTask and AlarmTask shared one, each would see only
// some of the readings.
static QueueHandle_t displayQueue;
static QueueHandle_t alarmQueue;

// Encoder steps from the EXTI1 ISR to InputTask: +1 clockwise, -1 counterclockwise.
static QueueHandle_t encoderQueue;
// The page InputTask selected. One slot, overwritten on every change, so
// DisplayTask always gets the latest page and never a backlog of old ones.
static QueueHandle_t modeQueue;

// systemEvents: system-wide event bits (lab step 35).
//
// EVENT_ACTIVE (bit 0): the system is ACTIVE.
//   Producer:  MotionTask. Set at start-up and when motion ends INACTIVE;
//              cleared when the inactivity timeout starts INACTIVE.
//   Consumers: SensorTask blocks on it while it is clear, pausing acquisition.
//              InputTask ignores encoder steps while it is clear.
//
// EVENT_MOTION (bit 1): the PIR output is high, i.e. motion now or within the
// PIR's hold time.
//   Producer:  MotionTask. Set when a poll finds the PIR output high, cleared
//              when a poll finds it low. Polls are MOTION_POLL_MS apart.
//   Consumer:  SensorTask copies it into SensorData.motionDetected, so
//              MotionTask is the only task that reads the PIR pin.
//
// EVENT_ALARM (bit 2): the latest evaluated reading is outside the normal
// temperature range.
//   Producer:  AlarmTask. Set when a reading evaluates to LOW_TEMPERATURE or
//              HIGH_TEMPERATURE, cleared when one evaluates to NORMAL. There
//              are no readings while INACTIVE, so it then keeps its last value;
//              act on it only while EVENT_ACTIVE is set too.
//   Consumer:  DisplayTask shows an alarm banner in place of the OLED title.
//              AlarmTask outranks SensorTask and DisplayTask, so it gets each
//              reading and updates the bit before DisplayTask draws that
//              reading.
#define EVENT_ACTIVE (1u << 0)
#define EVENT_MOTION (1u << 1)
#define EVENT_ALARM (1u << 2)
static EventGroupHandle_t systemEvents;

// DisplayTask blocks on a queue set, which can't include an event group, so
// MotionTask also sends each state change to it here (one slot, overwritten)
// to turn the OLED off or on.
static QueueHandle_t systemStateQueue;
// Lets DisplayTask block on displayQueue, modeQueue and systemStateQueue at once.
static QueueSetHandle_t displayEvents;

void SystemClock_Config(void);
static void MX_USART1_UART_Init(void);
static void MX_Encoder_Init(void);
static void MX_PIR_Init(void);

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

// Prints one line prefixed with the tick time. USART1 is shared by every task
// that logs, and all of them print through here (lab step 36).
//
// serialMutex makes each line one uninterrupted transmission. Without it, a
// task that preempted another mid-line would find the UART busy and its line
// would be lost or mixed into the other one. It is a mutex rather than a binary
// semaphore for priority inheritance: while a high-priority task (AlarmTask)
// waits for a line from a low-priority one (DisplayTask), the holder runs at
// the waiter's priority, so mid-priority tasks (ProcessingTask) can't stretch
// the wait. The line is formatted before taking the mutex, so the mutex is held
// only while the bytes go out (up to about 7 ms for a full line at 115200 baud).
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

    xSemaphoreTake(serialMutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart1, (uint8_t *)line, len, 100);
    xSemaphoreGive(serialMutex);
}

static const char *modeName(DisplayMode mode)
{
    switch (mode)
    {
    case DisplayMode::TEMPERATURE:
        return "Temperature";
    case DisplayMode::HUMIDITY:
        return "Humidity";
    case DisplayMode::LIGHT:
        return "Light";
    default:
        return "Motion";
    }
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

// The PIR module drives OUT high while it senses motion, and for its hold time
// (a few seconds) after. Only MotionTask calls this; others use EVENT_MOTION.
static bool pirMotionDetected(void)
{
    return HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN) == GPIO_PIN_SET;
}

static bool systemIsActive(void)
{
    return (xEventGroupGetBits(systemEvents) & EVENT_ACTIVE) != 0;
}

// Reads every sensor once. Failures are logged here, where the reason is known;
// the reading carries only a "no value" marker.
static SensorData readSensors(void)
{
    SensorData data = {NAN, NAN, -1, (xEventGroupGetBits(systemEvents) & EVENT_MOTION) != 0};

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
        // INACTIVE: acquisition pauses, so DisplayTask and AlarmTask get no
        // readings either. Blocked here, not polling, until motion restores
        // ACTIVE; then the period restarts instead of catching up on the
        // periods it slept through.
        if (!systemIsActive())
        {
            xEventGroupWaitBits(systemEvents, EVENT_ACTIVE, pdFALSE, pdTRUE, portMAX_DELAY);
            lastWakeTime = xTaskGetTickCount();
        }

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

// Draws one page: the title, the page number, the page's name and its value
// (lab steps 27 and 29). Temperature and humidity get one decimal place, the
// DHT22's resolution.
static HAL_StatusTypeDef showOnOled(DisplayMode mode, const SensorData &data)
{
    char number[16], value[20] = "", position[8];
    switch (mode)
    {
    case DisplayMode::TEMPERATURE:
        snprintf(value, sizeof value, "%s C", formatDecimal(number, sizeof number, data.temperature, 1));
        break;
    case DisplayMode::HUMIDITY:
        snprintf(value, sizeof value, "%s %%", formatDecimal(number, sizeof number, data.humidity, 1));
        break;
    case DisplayMode::LIGHT:
        if (data.lightLevel >= 0)
        {
            snprintf(value, sizeof value, "%d %%", data.lightLevel);
        }
        else
        {
            snprintf(value, sizeof value, "-- %%");
        }
        break;
    case DisplayMode::MOTION:
        snprintf(value, sizeof value, "%s", data.motionDetected ? "Yes" : "No");
        break;
    }
    snprintf(position, sizeof position, "%d/%d", static_cast<int>(mode) + 1, DISPLAY_MODE_COUNT);

    bool alarm = (xEventGroupGetBits(systemEvents) & EVENT_ALARM) != 0;

    ssd1306Clear();
    ssd1306DrawText(0, 0, alarm ? "!! ALARM !!" : "ROOM MONITOR", 1);
    ssd1306DrawText(SSD1306_WIDTH - 3 * SSD1306_CHAR_WIDTH, 0, position, 1);
    ssd1306DrawText(0, 24, modeName(mode), 1);
    ssd1306DrawText(0, 36, value, 2);
    return ssd1306Update();
}

// --- Display Task Definition ---
// First consumer, and the owner of the OLED: no other task initialises or draws
// on it, so the display driver needs no mutex (lab step 26). It keeps the latest
// reading and the current page, and redraws when either changes. Each reading
// also goes to the serial port. While the system is INACTIVE the OLED is off
// and nothing is drawn (lab step 34).
void DisplayTask(void *pvParameters)
{
    SensorData data = {NAN, NAN, -1, false};
    DisplayMode mode = DisplayMode::TEMPERATURE;
    SystemState systemState = SystemState::ACTIVE;
    uint32_t received = 0;

    bool oledReady = ssd1306Init() == HAL_OK && showOnOled(mode, data) == HAL_OK;
    if (!oledReady)
    {
        logPrintf("Display: OLED not responding, serial output only");
    }

    for (;;)
    {
        // Blocked until a reading or a page change arrives, so this never polls.
        QueueSetMemberHandle_t ready = xQueueSelectFromSet(displayEvents, portMAX_DELAY);
        if (ready == displayQueue)
        {
            xQueueReceive(displayQueue, &data, 0);
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
        }
        else if (ready == modeQueue)
        {
            xQueueReceive(modeQueue, &mode, 0);
        }
        else
        {
            xQueueReceive(systemStateQueue, &systemState, 0);
            if (oledReady &&
                ssd1306SetDisplayOn(systemState == SystemState::ACTIVE) != HAL_OK)
            {
                logPrintf("Display: OLED on/off failed");
            }
        }

        if (oledReady && systemState == SystemState::ACTIVE && showOnOled(mode, data) != HAL_OK)
        {
            logPrintf("Display: OLED update failed");
        }
    }
}

// Moves one page forward (step +1) or back (step -1), wrapping around.
static DisplayMode stepMode(DisplayMode mode, int step)
{
    int index = (static_cast<int>(mode) + step + DISPLAY_MODE_COUNT) % DISPLAY_MODE_COUNT;
    return static_cast<DisplayMode>(index);
}

// --- Input Task Definition ---
// Owns the page selection. Blocked until the encoder ISR queues a step, then
// moves one page and hands the new page to DisplayTask, which owns the OLED.
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
        mode = stepMode(mode, step);
        xQueueOverwrite(modeQueue, &mode);
        logPrintf("Input: %s, page %s", step > 0 ? "clockwise" : "counterclockwise", modeName(mode));
    }
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
// with vTaskDelayUntil and feeds nextSystemState in lib/system_state, which
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

        SystemState next = nextSystemState(state, motion, (now - lastMotion) * portTICK_PERIOD_MS,
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

// --- Alarm Task Definition ---
// Second consumer. The decision itself is evaluateTemperature in
// lib/alarm_logic, which has no hardware code and is unit tested on the host;
// this task feeds it readings and publishes the result as EVENT_ALARM. No
// buzzer yet: the state also goes to the serial port. The count should match
// DisplayTask's.
void AlarmTask(void *pvParameters)
{
    SensorData data;
    uint32_t received = 0;

    for (;;)
    {
        xQueueReceive(alarmQueue, &data, portMAX_DELAY);
        received++;

        if (isnan(data.temperature))
        {
            logPrintf("Alarm #%lu: no temperature reading, not evaluated", (unsigned long)received);
            continue;
        }

        // Update the bit before logging, which can block on the UART mutex.
        AlarmState state = evaluateTemperature(data.temperature);
        if (state == AlarmState::NORMAL)
        {
            xEventGroupClearBits(systemEvents, EVENT_ALARM);
        }
        else
        {
            xEventGroupSetBits(systemEvents, EVENT_ALARM);
        }

        char temperature[16];
        logPrintf("Alarm #%lu: %s C -> %s", (unsigned long)received,
                  formatDecimal(temperature, sizeof temperature, data.temperature, 1),
                  alarmStateName(state));
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

    // Direct writes are safe here without serialMutex: the scheduler hasn't
    // started, so nothing else can be using the UART.
    const char *msg1 = "BCA182 FreeRTOS Multisensor\r\n";
    const char *msg2 = "System starting...\r\n";

    HAL_UART_Transmit(&huart1, (uint8_t *)msg1, strlen(msg1), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)msg2, strlen(msg2), 100);
    if (ldrStatus != HAL_OK)
    {
        const char *msg = "Warning: LDR ADC setup or calibration failed\r\n";
        HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    }

    serialMutex = xSemaphoreCreateMutex();
    displayQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData));
    alarmQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData));
    encoderQueue = xQueueCreate(ENCODER_QUEUE_LENGTH, sizeof(int8_t));
    modeQueue = xQueueCreate(1, sizeof(DisplayMode));
    systemEvents = xEventGroupCreate();
    systemStateQueue = xQueueCreate(1, sizeof(SystemState));
    displayEvents = xQueueCreateSet(SENSOR_QUEUE_LENGTH + 1 + 1);
    if (displayEvents != NULL && displayQueue != NULL && modeQueue != NULL &&
        systemStateQueue != NULL)
    {
        xQueueAddToSet(displayQueue, displayEvents);
        xQueueAddToSet(modeQueue, displayEvents);
        xQueueAddToSet(systemStateQueue, displayEvents);
    }
    MX_Encoder_Init(); // after encoderQueue exists: its interrupt sends to it
    MX_PIR_Init();

    // Create Tasks and catch potential memory errors. 256-word stacks leave
    // room for the formatting in logPrintf.
    BaseType_t retA = xTaskCreate(TaskA, "TaskA", 256, NULL, 1, NULL);
    BaseType_t retB = xTaskCreate(TaskB, "TaskB", 256, NULL, 1, NULL);
    BaseType_t retS = xTaskCreate(SensorTask, "Sensor", 256, NULL, SENSOR_TASK_PRIORITY, &sensorTaskHandle);
    BaseType_t retM = xTaskCreate(StateMonitorTask, "Monitor", 256, NULL, MONITOR_TASK_PRIORITY, NULL);
    BaseType_t retP = xTaskCreate(ProcessingTask, "Process", 256, NULL, PROCESSING_TASK_PRIORITY, NULL);
    BaseType_t retD = xTaskCreate(DisplayTask, "Display", 256, NULL, DISPLAY_TASK_PRIORITY, NULL);
    BaseType_t retL = xTaskCreate(AlarmTask, "Alarm", 256, NULL, ALARM_TASK_PRIORITY, NULL);
    BaseType_t retI = xTaskCreate(InputTask, "Input", 256, NULL, INPUT_TASK_PRIORITY, NULL);
    BaseType_t retO = xTaskCreate(MotionTask, "Motion", 256, NULL, MOTION_TASK_PRIORITY, NULL);

    if (serialMutex == NULL || displayQueue == NULL || alarmQueue == NULL || encoderQueue == NULL ||
        modeQueue == NULL || systemEvents == NULL || systemStateQueue == NULL ||
        displayEvents == NULL || retA != pdPASS || retB != pdPASS || retS != pdPASS ||
        retM != pdPASS || retP != pdPASS || retD != pdPASS || retL != pdPASS || retI != pdPASS ||
        retO != pdPASS)
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

// --- Rotary Encoder Pins ---
static void MX_Encoder_Init(void)
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

// --- PIR Motion Sensor Pin ---
static void MX_PIR_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // Pull-down: a disconnected sensor reads as "no motion".
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PIR_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(PIR_PORT, &GPIO_InitStruct);
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