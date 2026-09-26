#include "sensors.h"
#include "dht22.h"
#include "ldr.h"
#include "log.h"
#include "motion.h"
#include "rtos_objects.h"
#include <math.h>

#define SENSOR_TASK_PERIOD_MS 2000 // also the DHT22's minimum time between reads

HAL_StatusTypeDef sensorsInit(void)
{
    dht22Init();
    return ldrInit();
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
        // lower-priority tasks (DisplayTask, Idle) run instead. When the
        // period is up, the tick interrupt makes it READY, and it resumes once
        // no higher-priority task is ready.
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}
