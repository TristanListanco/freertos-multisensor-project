#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Passive buzzer on PA6, driven with a 2 kHz square wave from TIM3 channel 1
   (lab FR-07). A passive buzzer only sounds while its input keeps switching, so
   a steady level is silent.

   Not thread-safe: AlarmTask owns the buzzer and is the only task that may call
   buzzerSet. */

/* Sets up PA6 and TIM3 with the buzzer off. Hardware initialisation: call from
   app_main before the scheduler. */
HAL_StatusTypeDef buzzerInit(void);

/* Starts or stops the tone. */
HAL_StatusTypeDef buzzerSet(bool on);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
