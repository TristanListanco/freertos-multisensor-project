// BCA182 FreeRTOS multisensor room monitor.
//
// This file only brings the system up (lab step 41): app_main() does hardware
// initialisation, FreeRTOS object creation, task creation, then starts the
// scheduler. The application lives in the modules (lab step 40):
//
//   sensors       SensorData and SensorTask (DHT22, LDR)
//   display       DisplayTask (owns the OLED)
//   input         InputTask and the rotary encoder interrupt
//   alarm         AlarmTask (owns the buzzer)
//   motion        MotionTask (PIR) and the ACTIVE/INACTIVE system state
//   rtos_objects  queues, queue sets, event group and mutex shared between tasks
//   log           serial output shared by every task
//   demo_tasks    ProcessingTask, from the step 19 exercise
//
// Pure decision logic sits in lib/ rather than src/ so it also builds on the
// host for `pio test` (lab step 42): lib/alarm_logic (temperature alarm and
// buzzer rule), lib/display_navigation (DisplayMode and page changes) and
// lib/system_state (the ACTIVE/INACTIVE state machine). Device drivers are
// dht22, ldr, ssd1306 and buzzer.

#include "stm32f1xx_hal.h"
#include "alarm.h"
#include "buzzer.h"
#include "demo_tasks.h"
#include "display.h"
#include "input.h"
#include "log.h"
#include "motion.h"
#include "rtos_objects.h"
#include "sensors.h"

// Task priorities (lab step 38). A higher number preempts a lower one; Idle is 0.
// Each is set by how soon the task must run once it has work (urgency), how
// late it may run without harm (acceptable latency), and how long its own work
// can hold up the tasks below it.
//
// 3  MotionTask   Brings the system back to ACTIVE; a late poll leaves the OLED
//                 dark after someone walks in. Aims for well under a second.
//                 Each poll takes microseconds, so it costs the others nothing.
// 3  InputTask    Turns an encoder step into a page change, which a user
//                 expects within about 100 ms. Runs only when the knob turns.
// 2  SensorTask   Periodic 2 s acquisition; a few ms of jitter in when a period
//                 starts is harmless. The DHT22's microsecond-timed read is
//                 protected by suspending the scheduler, not by priority.
// 2  AlarmTask    Evaluates each new reading. Readings come every 2 s, so
//                 responding within a fraction of a second is plenty. It must
//                 outrank DisplayTask, so EVENT_ALARM is updated before the
//                 reading is drawn.
// 1  DisplayTask  A human reads the screen, so tens of ms of delay go unnoticed.
//                 Each OLED update busy-waits on I2C (about 25 ms on hardware,
//                 160 ms in Wokwi); at the bottom that never holds up the tasks
//                 above.
//
// Earlier lab exercise, kept for its demonstration:
// 2  ProcessingTask    (step 19) continuous work beside SensorTask, blocking
//                      after each batch so the tasks below still run.
#define MOTION_TASK_PRIORITY 3
#define INPUT_TASK_PRIORITY 3
#define SENSOR_TASK_PRIORITY 2
#define ALARM_TASK_PRIORITY 2
#define DISPLAY_TASK_PRIORITY 1
#define PROCESSING_TASK_PRIORITY 2

// 256 words (1 KB) per task leaves room for the formatting in logPrintf.
#define TASK_STACK_WORDS 256

HAL_StatusTypeDef SystemClock_Config(void);

extern "C" void xPortSysTickHandler(void);

// SysTick drives the HAL tick at all times, but only feeds FreeRTOS once the
// scheduler is running. Calling xPortSysTickHandler earlier makes the kernel
// touch a NULL pxCurrentTCB and pend a PendSV with no task to switch to.
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}

// Reports a start-up failure and stops: the system can't run without what
// failed.
[[noreturn]] static void halt(const char *message)
{
    logWriteDirect(message);
    for (;;)
    {
    }
}

static bool createTask(TaskFunction_t task, const char *name, UBaseType_t priority,
                       TaskHandle_t *handle = nullptr)
{
    return xTaskCreate(task, name, TASK_STACK_WORDS, NULL, priority, handle) == pdPASS;
}

// The application entry point (lab section 10). It never returns: it ends by
// handing the CPU to the FreeRTOS scheduler.
[[noreturn]] static void app_main(void)
{
    // 1. Hardware initialisation.
    HAL_StatusTypeDef clockStatus = SystemClock_Config();
    SystemCoreClockUpdate(); // FreeRTOS derives its tick rate from SystemCoreClock
    logInit();
    HAL_StatusTypeDef sensorStatus = sensorsInit();
    HAL_StatusTypeDef buzzerStatus = buzzerInit(); // after the clock: its tone is set from PCLK1
    motionInit();

    // Direct writes are safe here without serialMutex: the scheduler hasn't
    // started, so nothing else can be using the UART.
    logWriteDirect("BCA182 FreeRTOS Multisensor\r\n");
    logWriteDirect("System starting...\r\n");
    if (clockStatus != HAL_OK)
    {
        logWriteDirect("Warning: clock configuration failed\r\n");
    }
    if (sensorStatus != HAL_OK)
    {
        logWriteDirect("Warning: LDR ADC setup or calibration failed\r\n");
    }
    if (buzzerStatus != HAL_OK)
    {
        logWriteDirect("Warning: buzzer timer setup failed\r\n");
    }

    // 2. FreeRTOS object creation.
    if (!createRtosObjects())
    {
        halt("FreeRTOS object creation failed\r\n");
    }
    inputInit(); // enables the encoder interrupt, which sends to encoderQueue, so after it exists

    // 3. Task creation.
    bool tasksCreated =
        createTask(SensorTask, "Sensor", SENSOR_TASK_PRIORITY) &&
        createTask(ProcessingTask, "Process", PROCESSING_TASK_PRIORITY) &&
        createTask(DisplayTask, "Display", DISPLAY_TASK_PRIORITY) &&
        createTask(AlarmTask, "Alarm", ALARM_TASK_PRIORITY) &&
        createTask(InputTask, "Input", INPUT_TASK_PRIORITY) &&
        createTask(MotionTask, "Motion", MOTION_TASK_PRIORITY);
    if (!tasksCreated)
    {
        halt("Task creation failed\r\n");
    }

    // 4. Scheduler-driven operation. vTaskStartScheduler only returns if it
    // can't allocate the Idle task.
    vTaskStartScheduler();
    halt("Scheduler failed to start!\r\n");
}

// The C entry point: the startup code calls main() after reset. The lab
// handout names app_main() as the application entry point (that is ESP-IDF's
// convention, where the framework calls it); on STM32Cube nothing calls
// app_main() for us, so main() resets the HAL and hands over to it.
int main(void)
{
    HAL_Init();
    app_main();
}

// --- System Clock Configuration ---
// Everything runs from the 8 MHz internal oscillator (HSI) with no dividers:
// SYSCLK = HCLK = PCLK1 = PCLK2 = 8 MHz.
//
// The chip already runs from HSI after reset, so only the bus dividers are set
// and SYSCLK is left alone. Selecting HSI again fails in Wokwi: its RCC model
// never reports HSI as ready, so HAL_RCC_ClockConfig returns HAL_ERROR after it
// has already put both APB buses on their interim /16 setting. That left PCLK1
// at 500 kHz, below the I2C peripheral's 4 MHz minimum for 400 kHz, and the
// OLED could not start.
HAL_StatusTypeDef SystemClock_Config(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    return HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}
