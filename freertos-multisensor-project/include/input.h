#ifndef INPUT_H
#define INPUT_H

// Rotary encoder on PA1 (CLK, EXTI1) and PA2 (DT) (lab steps 28-29).

// Sets up the encoder pins and enables its interrupt, which sends to
// encoderQueue: call from main after createRtosObjects.
void inputInit(void);

// Owns the page selection: turns encoder steps into page changes for
// DisplayTask while the system is ACTIVE.
void InputTask(void *pvParameters);

#endif // INPUT_H
