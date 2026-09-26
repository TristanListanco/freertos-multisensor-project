#ifndef ALARM_H
#define ALARM_H

// Evaluates every reading with the temperature alarm logic in lib/alarm_logic
// and publishes the result as EVENT_ALARM (lab steps 30 and 35).
void AlarmTask(void *pvParameters);

#endif // ALARM_H
