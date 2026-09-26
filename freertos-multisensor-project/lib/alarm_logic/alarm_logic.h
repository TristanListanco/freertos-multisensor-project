#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H

// Temperature alarm decisions (lab step 30). Pure logic: no HAL, FreeRTOS or
// buzzer code, so it builds for the host and is unit tested there
// (test/test_alarm_logic, run with `pio test -e native`).

enum class AlarmState
{
    NORMAL,
    LOW_TEMPERATURE,
    HIGH_TEMPERATURE
};

// Normal range, inclusive at both ends: exactly 18.0 C or 30.0 C is NORMAL.
constexpr float ALARM_LOW_TEMPERATURE_C = 18.0f;
constexpr float ALARM_HIGH_TEMPERATURE_C = 30.0f;

// Classifies one temperature reading in degrees C. The caller must not pass NaN
// (a failed sensor read); it is neither above nor below any threshold, so it
// would come back NORMAL.
AlarmState evaluateTemperature(float temperature);

const char *alarmStateName(AlarmState state);

#endif // ALARM_LOGIC_H
