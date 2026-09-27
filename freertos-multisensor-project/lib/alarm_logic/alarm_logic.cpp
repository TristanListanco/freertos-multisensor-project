#include "alarm_logic.h"

AlarmState evaluateTemperature(float temperature)
{
    if (temperature < ALARM_LOW_TEMPERATURE_C)
    {
        return AlarmState::LOW_TEMPERATURE;
    }
    if (temperature > ALARM_HIGH_TEMPERATURE_C)
    {
        return AlarmState::HIGH_TEMPERATURE;
    }
    return AlarmState::NORMAL;
}

bool alarmShouldSound(AlarmState state, bool systemActive)
{
    return systemActive && state != AlarmState::NORMAL;
}

const char *alarmStateName(AlarmState state)
{
    switch (state)
    {
    case AlarmState::NORMAL:
        return "NORMAL";
    case AlarmState::LOW_TEMPERATURE:
        return "LOW_TEMPERATURE";
    default:
        return "HIGH_TEMPERATURE";
    }
}
