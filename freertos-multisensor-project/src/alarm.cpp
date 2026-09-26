#include "alarm.h"
#include "alarm_logic.h"
#include "log.h"
#include "rtos_objects.h"
#include "sensors.h"
#include <math.h>

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
