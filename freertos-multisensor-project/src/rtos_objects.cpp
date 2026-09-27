#include "rtos_objects.h"
#include "display.h"
#include "sensors.h"
#include "system_state.h"

SemaphoreHandle_t serialMutex;
QueueHandle_t displayQueue;
QueueHandle_t alarmQueue;
QueueHandle_t encoderQueue;
QueueHandle_t modeQueue;
EventGroupHandle_t systemEvents;
QueueHandle_t displayStateQueue;
QueueHandle_t alarmStateQueue;
QueueSetHandle_t displayEvents;
QueueSetHandle_t alarmEvents;

bool createRtosObjects(void)
{
    serialMutex = xSemaphoreCreateMutex();
    displayQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData));
    alarmQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData));
    encoderQueue = xQueueCreate(ENCODER_QUEUE_LENGTH, sizeof(int8_t));
    modeQueue = xQueueCreate(1, sizeof(DisplayMode));
    systemEvents = xEventGroupCreate();
    displayStateQueue = xQueueCreate(1, sizeof(SystemState));
    alarmStateQueue = xQueueCreate(1, sizeof(SystemState));
    // Each set has room for every item its member queues can hold at once.
    displayEvents = xQueueCreateSet(SENSOR_QUEUE_LENGTH + 1 + 1);
    alarmEvents = xQueueCreateSet(SENSOR_QUEUE_LENGTH + 1);

    if (serialMutex == NULL || displayQueue == NULL || alarmQueue == NULL ||
        encoderQueue == NULL || modeQueue == NULL || systemEvents == NULL ||
        displayStateQueue == NULL || alarmStateQueue == NULL || displayEvents == NULL ||
        alarmEvents == NULL)
    {
        return false;
    }

    return xQueueAddToSet(displayQueue, displayEvents) == pdPASS &&
           xQueueAddToSet(modeQueue, displayEvents) == pdPASS &&
           xQueueAddToSet(displayStateQueue, displayEvents) == pdPASS &&
           xQueueAddToSet(alarmQueue, alarmEvents) == pdPASS &&
           xQueueAddToSet(alarmStateQueue, alarmEvents) == pdPASS;
}
