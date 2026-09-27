#include "alarm.h"
#include "alarm_logic.h"
#include "buzzer.h"
#include "log.h"
#include "rtos_objects.h"
#include "sensors.h"
#include "system_state.h"
#include <math.h>

// --- Alarm Task Definition ---
// Second consumer of the readings, and the owner of the buzzer: no other task
// switches it, so the buzzer driver needs no mutex (FR-07). The decisions are
// pure functions in lib/alarm_logic, unit tested on the host:
// evaluateTemperature classifies each reading, and alarmShouldSound adds that
// the alarm is only active while the system is ACTIVE (lab steps 33-34). This
// task feeds them, publishes the alarm as EVENT_ALARM and switches the buzzer.
// The reading count should match DisplayTask's.
void AlarmTask(void *pvParameters)
{
    SensorData data;
    AlarmState alarm = AlarmState::NORMAL;
    SystemState systemState = SystemState::ACTIVE;
    bool buzzing = false;
    uint32_t received = 0;

    for (;;)
    {
        // Blocked until a reading or a state change arrives, so this never
        // polls. A state change needs no reading: going INACTIVE silences the
        // buzzer at once.
        QueueSetMemberHandle_t ready = xQueueSelectFromSet(alarmEvents, portMAX_DELAY);
        if (ready == alarmQueue)
        {
            xQueueReceive(alarmQueue, &data, 0);
            received++;

            if (isnan(data.temperature))
            {
                // A failed read says nothing about the temperature, so the
                // alarm keeps its last state.
                logPrintf("Alarm #%lu: no temperature reading, state unchanged",
                          (unsigned long)received);
            }
            else
            {
                // Update the bit before logging, which can block on the UART
                // mutex.
                alarm = evaluateTemperature(data.temperature);
                if (alarm == AlarmState::NORMAL)
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
                          alarmStateName(alarm));
            }
        }
        else
        {
            xQueueReceive(alarmStateQueue, &systemState, 0);
        }

        bool sound = alarmShouldSound(alarm, systemState == SystemState::ACTIVE);
        if (sound != buzzing)
        {
            buzzing = sound;
            if (buzzerSet(sound) == HAL_OK)
            {
                logPrintf("Alarm: buzzer %s", sound ? "on" : "off");
            }
            else
            {
                logPrintf("Alarm: buzzer %s failed", sound ? "on" : "off");
            }
        }
    }
}
