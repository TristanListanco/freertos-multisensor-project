#include "system_state.h"

SystemState evaluateSystemState(SystemState current, bool motion, uint32_t msSinceMotion,
                                uint32_t timeoutMs)
{
    if (motion)
    {
        return SystemState::ACTIVE;
    }
    if (current == SystemState::ACTIVE && msSinceMotion >= timeoutMs)
    {
        return SystemState::INACTIVE;
    }
    return current;
}

const char *systemStateName(SystemState state)
{
    return state == SystemState::ACTIVE ? "ACTIVE" : "INACTIVE";
}
