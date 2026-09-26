#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>

// ACTIVE/INACTIVE state machine driven by the PIR (lab step 32). Pure logic,
// unit tested on the host (test/test_system_state, `pio test -e native`).
//
//   ACTIVE   --(no motion for timeoutMs)--> INACTIVE
//   INACTIVE --(motion detected)----------> ACTIVE

enum class SystemState
{
    ACTIVE,
    INACTIVE
};

// One step of the state machine.
//   motion:        the PIR output is high right now.
//   msSinceMotion: time since the PIR output was last seen high.
//   timeoutMs:     inactivity timeout.
SystemState nextSystemState(SystemState current, bool motion, uint32_t msSinceMotion,
                            uint32_t timeoutMs);

const char *systemStateName(SystemState state);

#endif // SYSTEM_STATE_H
