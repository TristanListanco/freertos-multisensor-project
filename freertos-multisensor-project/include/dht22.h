#ifndef DHT22_H
#define DHT22_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DHT22_OK,
    DHT22_NO_RESPONSE,  /* the sensor never answered the start signal */
    DHT22_TIMEOUT,      /* the sensor stopped partway through the 40 bits */
    DHT22_BAD_CHECKSUM, /* all 40 bits arrived but the checksum doesn't match */
} Dht22Status;

/* Configures the data pin (PB12) and lets the line idle high. Call before
   dht22Read. */
void dht22Init(void);

/* Reads one sample, in tenths: 254 means 25.4 C, 612 means 61.2 %RH. Call it
   from a task only, since it blocks while it sends the start signal. The
   sensor needs at least 2 s between reads. */
Dht22Status dht22Read(int16_t *temperatureTenths, uint16_t *humidityTenths);

const char *dht22StatusName(Dht22Status status);

#ifdef __cplusplus
}
#endif

#endif /* DHT22_H */
