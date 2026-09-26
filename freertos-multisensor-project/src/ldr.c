/* Photoresistor (LDR) module on PA0, sampled by ADC1 channel 0.

   The module puts the LDR in a voltage divider with a fixed resistor and
   outputs the midpoint on AO. More light lowers the LDR's resistance, which
   lowers AO. The module runs from 3.3 V so AO stays inside the ADC's 0-3.3 V
   input range; PA0 is not 5 V tolerant.

   Light level, 0-100 %: 0 % when the ADC reads full scale (AO at 3.3 V, dark)
   and 100 % when it reads 0 (AO at 0 V, bright), linear in the ADC reading in
   between. It is a relative, uncalibrated level, not lux:
     - An LDR's resistance falls roughly as a power of the illuminance, so equal
       steps in percent are not equal steps in lux.
     - Converting to lux needs the fixed resistor's value and the LDR's
       resistance curve from its datasheet, or a calibration against a lux
       meter. This project has neither. */

#include "ldr.h"

static ADC_HandleTypeDef hadc1;

HAL_StatusTypeDef ldrInit(void)
{
    /* 8 MHz PCLK2 / 2 = 4 MHz; the ADC clock must not exceed 14 MHz. */
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    clk.AdcClockSelection = RCC_ADCPCLK2_DIV2;
    HAL_RCCEx_PeriphCLKConfig(&clk);

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* One software-triggered conversion per ldrRead. */
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* The longest sampling time, because the divider's output resistance is in
       the kilohms. A conversion then takes (239.5 + 12.5) / 4 MHz = 63 us. */
    ADC_ChannelConfTypeDef channel = {0};
    channel.Channel = ADC_CHANNEL_0;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Measures and removes the ADC's offset error. */
    return HAL_ADCEx_Calibration_Start(&hadc1);
}

HAL_StatusTypeDef ldrRead(uint16_t *raw)
{
    HAL_ADC_Start(&hadc1);
    HAL_StatusTypeDef status = HAL_ADC_PollForConversion(&hadc1, 10);
    if (status == HAL_OK)
    {
        *raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return status;
}

uint8_t ldrLightPercent(uint16_t raw)
{
    /* Rounded to the nearest percent. */
    return (uint8_t)(((LDR_ADC_MAX - raw) * 100u + LDR_ADC_MAX / 2) / LDR_ADC_MAX);
}
