/* Buzzer driver: TIM3 channel 1 PWM on PA6 at 2 kHz, 50 % duty.

   TIM3 sits on APB1, which runs at 8 MHz with no divider, so the timer counts
   at 8 MHz. A prescaler of 8 gives 1 MHz ticks, and a period of 500 ticks gives
   2 kHz. While the channel is stopped its output is held low. */

#include "buzzer.h"

#define BUZZER_TIMER_TICK_HZ 1000000u
#define BUZZER_TONE_HZ 2000u
#define BUZZER_PERIOD_TICKS (BUZZER_TIMER_TICK_HZ / BUZZER_TONE_HZ)

static TIM_HandleTypeDef htim3;

HAL_StatusTypeDef buzzerInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6; /* TIM3_CH1, no remap */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = HAL_RCC_GetPCLK1Freq() / BUZZER_TIMER_TICK_HZ - 1;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = BUZZER_PERIOD_TICKS - 1;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
    {
        return HAL_ERROR;
    }

    TIM_OC_InitTypeDef channel = {0};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = BUZZER_PERIOD_TICKS / 2; /* 50 % duty: a square wave */
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    return HAL_TIM_PWM_ConfigChannel(&htim3, &channel, TIM_CHANNEL_1);
}

HAL_StatusTypeDef buzzerSet(bool on)
{
    return on ? HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) : HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
}
