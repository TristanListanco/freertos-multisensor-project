/* DHT22 (AM2302) temperature and humidity sensor on a single data line.

   Protocol: the MCU holds the line low for at least 1 ms, then releases it.
   The sensor answers with 80 us low and 80 us high, then sends 40 bits. Each
   bit is 50 us low followed by 26-28 us high for a 0 or 70 us high for a 1.
   The bits are humidity (16), temperature (16, top bit is the sign) and a
   checksum (8). Both values are in tenths.

   A bit is decoded by comparing how long the line stays high with how long
   the 50 us low before it lasted, both counted in polling-loop passes. This
   needs no timer and works at any CPU clock. Interrupts still run during a
   read, including in Wokwi, where PRIMASK masks nothing. An interrupt only
   hides some time from the count, and the SysTick handler is short compared
   with the gap between the pulse lengths. */

#include "dht22.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define DHT22_PORT GPIOB
#define DHT22_PIN GPIO_PIN_12

/* Polling-loop passes before a wait gives up. A pass is about 11 cycles, so at
   8 MHz this is about 1.4 ms, well over the longest (80 us) pulse. */
#define DHT22_TIMEOUT_LOOPS 1000u
#define DHT22_TIMED_OUT UINT32_MAX

static void setPinMode(uint32_t mode)
{
    GPIO_InitTypeDef init = {0};
    init.Pin = DHT22_PIN;
    init.Mode = mode;
    init.Pull = GPIO_PULLUP; /* used in input mode only */
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT22_PORT, &init);
}

/* Counts polling-loop passes until the line leaves `level`. It reads IDR
   directly because a HAL call on each pass would coarsen the count. */
static uint32_t pulseLength(uint32_t level)
{
    uint32_t count = 0;
    while ((DHT22_PORT->IDR & DHT22_PIN ? 1u : 0u) == level)
    {
        if (++count >= DHT22_TIMEOUT_LOOPS)
        {
            return DHT22_TIMED_OUT;
        }
    }
    return count;
}

void dht22Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    setPinMode(GPIO_MODE_INPUT); /* released: the pull-up holds the line high */
}

Dht22Status dht22Read(int16_t *temperatureTenths, uint16_t *humidityTenths)
{
    uint8_t data[5] = {0};
    Dht22Status status = DHT22_OK;

    /* Start signal. Blocking here instead of spinning lets other tasks run.
       vTaskDelay can end up to a tick early, so this is 2-3 ms. */
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_RESET);
    setPinMode(GPIO_MODE_OUTPUT_PP);
    vTaskDelay(pdMS_TO_TICKS(3));

    /* The reply is timed in microseconds, so no other task may run until it is
       over (about 5 ms). */
    vTaskSuspendAll();

    /* Release the line: drive it high, then hand it to the pull-up. */
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);
    setPinMode(GPIO_MODE_INPUT);

    /* Wait for the sensor to pull low, then through its 80 us low and 80 us
       high. */
    if (pulseLength(1) == DHT22_TIMED_OUT || pulseLength(0) == DHT22_TIMED_OUT ||
        pulseLength(1) == DHT22_TIMED_OUT)
    {
        status = DHT22_NO_RESPONSE;
    }

    for (int i = 0; i < 40 && status == DHT22_OK; i++)
    {
        uint32_t low = pulseLength(0);  /* 50 us before every bit */
        uint32_t high = pulseLength(1); /* 26-28 us for a 0, 70 us for a 1 */
        if (low == DHT22_TIMED_OUT || high == DHT22_TIMED_OUT)
        {
            status = DHT22_TIMEOUT;
        }
        data[i / 8] = (uint8_t)((data[i / 8] << 1) | (high > low));
    }

    xTaskResumeAll();

    if (status != DHT22_OK)
    {
        return status;
    }
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
    {
        return DHT22_BAD_CHECKSUM;
    }

    *humidityTenths = (uint16_t)((data[0] << 8) | data[1]);
    int16_t magnitude = (int16_t)(((data[2] & 0x7F) << 8) | data[3]);
    *temperatureTenths = (data[2] & 0x80) ? (int16_t)-magnitude : magnitude;
    return DHT22_OK;
}

const char *dht22StatusName(Dht22Status status)
{
    switch (status)
    {
    case DHT22_OK:
        return "OK";
    case DHT22_NO_RESPONSE:
        return "no response";
    case DHT22_TIMEOUT:
        return "timeout";
    default:
        return "bad checksum";
    }
}
