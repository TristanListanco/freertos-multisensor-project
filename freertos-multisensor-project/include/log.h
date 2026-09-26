#ifndef LOG_H
#define LOG_H

#include <stddef.h>

// Serial output on USART1 (PA9 TX, PA10 RX, 115200 baud). Every task logs
// through logPrintf, which serialises lines with serialMutex (lab step 36).

// Sets up USART1. Hardware initialisation: call from main before the scheduler.
void logInit(void);

// Writes text as-is, without serialMutex. Only for main(), before the scheduler
// starts or after it fails to, when nothing else can be using the UART.
void logWriteDirect(const char *text);

// Prints one line prefixed with the tick time. From tasks only.
void logPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Formats a value with 1 or 2 decimals into buf and returns buf, or returns
// "--" for NAN. newlib-nano's printf has no %f, so this prints the rounded
// value's digits as integers.
const char *formatDecimal(char *buf, size_t size, float value, int decimals);

#endif // LOG_H
