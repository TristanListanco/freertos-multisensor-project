#ifndef MOTION_H
#define MOTION_H

// PIR motion sensor on PA3 and the ACTIVE/INACTIVE system state it drives
// (lab steps 31-34). The state machine itself is evaluateSystemState in
// lib/system_state.

// Sets up the PIR pin. Hardware initialisation: call from main before the
// scheduler.
void motionInit(void);

// Owns the system state: polls the PIR and publishes ACTIVE/INACTIVE.
void MotionTask(void *pvParameters);

// True while the system is ACTIVE (EVENT_ACTIVE is set). Any task may call it.
bool systemIsActive(void);

#endif // MOTION_H
