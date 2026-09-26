#ifndef LDR_H
#define LDR_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LDR_ADC_MAX 4095u /* 12-bit ADC full scale */

/* Sets up ADC1 channel 0 on PA0 and calibrates the ADC. Readings still work if
   only the calibration fails, just without its offset correction. */
HAL_StatusTypeDef ldrInit(void);

/* Takes one ADC sample, 0 to LDR_ADC_MAX. */
HAL_StatusTypeDef ldrRead(uint16_t *raw);

/* Converts a raw sample to a light level from 0 to 100 %. This is a relative
   level, not lux; see ldr.c for what it means. */
uint8_t ldrLightPercent(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* LDR_H */
