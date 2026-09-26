#include "display.h"
#include "log.h"
#include "rtos_objects.h"
#include "sensors.h"
#include "ssd1306.h"
#include "system_state.h"
#include <math.h>
#include <stdio.h>

// Draws one page: the title, the page number, the page's name and its value
// (lab steps 27 and 29). Temperature and humidity get one decimal place, the
// DHT22's resolution.
static HAL_StatusTypeDef showOnOled(DisplayMode mode, const SensorData &data)
{
    char number[16], value[20] = "", position[8];
    switch (mode)
    {
    case DisplayMode::TEMPERATURE:
        snprintf(value, sizeof value, "%s C", formatDecimal(number, sizeof number, data.temperature, 1));
        break;
    case DisplayMode::HUMIDITY:
        snprintf(value, sizeof value, "%s %%", formatDecimal(number, sizeof number, data.humidity, 1));
        break;
    case DisplayMode::LIGHT:
        if (data.lightLevel >= 0)
        {
            snprintf(value, sizeof value, "%d %%", data.lightLevel);
        }
        else
        {
            snprintf(value, sizeof value, "-- %%");
        }
        break;
    case DisplayMode::MOTION:
        snprintf(value, sizeof value, "%s", data.motionDetected ? "Yes" : "No");
        break;
    }
    snprintf(position, sizeof position, "%d/%d", static_cast<int>(mode) + 1, DISPLAY_MODE_COUNT);

    bool alarm = (xEventGroupGetBits(systemEvents) & EVENT_ALARM) != 0;

    ssd1306Clear();
    ssd1306DrawText(0, 0, alarm ? "!! ALARM !!" : "ROOM MONITOR", 1);
    ssd1306DrawText(SSD1306_WIDTH - 3 * SSD1306_CHAR_WIDTH, 0, position, 1);
    ssd1306DrawText(0, 24, displayModeName(mode), 1);
    ssd1306DrawText(0, 36, value, 2);
    return ssd1306Update();
}

// --- Display Task Definition ---
// First consumer, and the owner of the OLED: no other task initialises or draws
// on it, so the display driver needs no mutex (lab step 26). It keeps the latest
// reading and the current page, and redraws when either changes. Each reading
// also goes to the serial port. While the system is INACTIVE the OLED is off
// and nothing is drawn (lab step 34).
void DisplayTask(void *pvParameters)
{
    SensorData data = {NAN, NAN, -1, false};
    DisplayMode mode = DisplayMode::TEMPERATURE;
    SystemState systemState = SystemState::ACTIVE;
    uint32_t received = 0;

    HAL_StatusTypeDef status = ssd1306Init();
    if (status == HAL_OK)
    {
        status = showOnOled(mode, data);
    }
    bool oledReady = status == HAL_OK;
    if (!oledReady)
    {
        logPrintf("Display: OLED init failed (HAL %d, I2C error 0x%02lx)", status,
                  (unsigned long)ssd1306LastError());
    }

    for (;;)
    {
        // Blocked until a reading, a page change or a state change arrives, so
        // this never polls.
        QueueSetMemberHandle_t ready = xQueueSelectFromSet(displayEvents, portMAX_DELAY);
        if (ready == displayQueue)
        {
            xQueueReceive(displayQueue, &data, 0);
            received++;

            char temperature[16], humidity[16], light[8] = "--";
            if (data.lightLevel >= 0)
            {
                snprintf(light, sizeof light, "%d", data.lightLevel);
            }
            logPrintf("Display #%lu: %s C, %s %%RH, light %s %%, motion %s", (unsigned long)received,
                      formatDecimal(temperature, sizeof temperature, data.temperature, 2),
                      formatDecimal(humidity, sizeof humidity, data.humidity, 2), light,
                      data.motionDetected ? "yes" : "no");
        }
        else if (ready == modeQueue)
        {
            xQueueReceive(modeQueue, &mode, 0);
        }
        else
        {
            xQueueReceive(systemStateQueue, &systemState, 0);
            if (oledReady &&
                ssd1306SetDisplayOn(systemState == SystemState::ACTIVE) != HAL_OK)
            {
                logPrintf("Display: OLED on/off failed (I2C error 0x%02lx)",
                          (unsigned long)ssd1306LastError());
            }
        }

        if (oledReady && systemState == SystemState::ACTIVE && showOnOled(mode, data) != HAL_OK)
        {
            logPrintf("Display: OLED update failed (I2C error 0x%02lx)",
                      (unsigned long)ssd1306LastError());
        }
    }
}
