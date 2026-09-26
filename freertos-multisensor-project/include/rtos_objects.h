#ifndef RTOS_OBJECTS_H
#define RTOS_OBJECTS_H

// FreeRTOS objects shared between tasks (lab step 40). main() creates them all
// with createRtosObjects before any task exists, and the handles never change
// afterwards, so tasks can read the handles without locking.

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#define SENSOR_QUEUE_LENGTH 4  // readings a consumer can fall behind by before new ones are dropped
#define ENCODER_QUEUE_LENGTH 8 // steps the encoder ISR can queue before InputTask catches up

// Guards USART1; see logPrintf in log.cpp (lab step 36).
extern SemaphoreHandle_t serialMutex;

// SensorData from SensorTask to each consumer (lab step 25). Each consumer gets
// its own queue: a FreeRTOS queue hands every item to exactly one receiver, so
// if DisplayTask and AlarmTask shared one, each would see only some readings.
extern QueueHandle_t displayQueue;
extern QueueHandle_t alarmQueue;

// int8_t encoder steps from the EXTI1 ISR to InputTask: +1 clockwise, -1
// counterclockwise.
extern QueueHandle_t encoderQueue;

// The DisplayMode InputTask selected. One slot, overwritten on every change, so
// DisplayTask always gets the latest page and never a backlog of old ones.
extern QueueHandle_t modeQueue;

// systemEvents: system-wide event bits (lab step 35).
//
// EVENT_ACTIVE (bit 0): the system is ACTIVE.
//   Producer:  MotionTask. Set at start-up and when motion ends INACTIVE;
//              cleared when the inactivity timeout starts INACTIVE.
//   Consumers: SensorTask blocks on it while it is clear, pausing acquisition.
//              InputTask ignores encoder steps while it is clear.
//
// EVENT_MOTION (bit 1): the PIR output is high, i.e. motion now or within the
// PIR's hold time.
//   Producer:  MotionTask. Set when a poll finds the PIR output high, cleared
//              when a poll finds it low. Polls are 100 ms apart.
//   Consumer:  SensorTask copies it into SensorData.motionDetected, so
//              MotionTask is the only task that reads the PIR pin.
//
// EVENT_ALARM (bit 2): the latest evaluated reading is outside the normal
// temperature range.
//   Producer:  AlarmTask. Set when a reading evaluates to LOW_TEMPERATURE or
//              HIGH_TEMPERATURE, cleared when one evaluates to NORMAL. There
//              are no readings while INACTIVE, so it then keeps its last value;
//              act on it only while EVENT_ACTIVE is set too.
//   Consumer:  DisplayTask shows an alarm banner in place of the OLED title.
//              AlarmTask outranks DisplayTask, so it has handled each reading
//              and updated the bit before DisplayTask can draw that reading.
#define EVENT_ACTIVE (1u << 0)
#define EVENT_MOTION (1u << 1)
#define EVENT_ALARM (1u << 2)
extern EventGroupHandle_t systemEvents;

// SystemState changes from MotionTask to DisplayTask. DisplayTask blocks on a
// queue set, which can't include an event group, so MotionTask also sends each
// change here (one slot, overwritten) to turn the OLED off or on.
extern QueueHandle_t systemStateQueue;

// Lets DisplayTask block on displayQueue, modeQueue and systemStateQueue at once.
extern QueueSetHandle_t displayEvents;

// Set by xTaskCreate in main(); StateMonitorTask samples this task's state.
extern TaskHandle_t sensorTaskHandle;

// Creates every object above except the task handle. Returns false if any
// allocation failed.
bool createRtosObjects(void);

#endif // RTOS_OBJECTS_H
