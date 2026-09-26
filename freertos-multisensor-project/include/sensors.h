#ifndef SENSORS_H
#define SENSORS_H

#include "stm32f1xx_hal.h"

// One set of readings, passed from SensorTask to each consumer (lab step 24).
struct SensorData
{
    float temperature;   // degrees C; NAN if the DHT22 read failed
    float humidity;      // % relative humidity; NAN if the DHT22 read failed
    int lightLevel;      // 0-100 %, relative, not lux (see ldr.c); -1 if the read failed
    bool motionDetected; // PIR output high when the reading was taken
};

// Sets up the DHT22 and the LDR's ADC. Hardware initialisation: call from main
// before the scheduler. Returns the LDR's result; the DHT22 has no setup to fail.
HAL_StatusTypeDef sensorsInit(void);

// Reads the DHT22 and LDR every 2 s while the system is ACTIVE and sends each
// reading to AlarmTask and DisplayTask (lab steps 22 and 25).
void SensorTask(void *pvParameters);

#endif // SENSORS_H
