#ifndef ALARM_H
#define ALARM_H

// Evaluates every reading with the temperature alarm logic in lib/alarm_logic,
// publishes the result as EVENT_ALARM and sounds the buzzer while the system is
// ACTIVE (lab steps 30 and 35, FR-07). Owns the buzzer.
void AlarmTask(void *pvParameters);

#endif // ALARM_H
