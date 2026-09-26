// BCA182 FreeRTOS multisensor room monitor.
//
// main() only brings the system up (lab step 41): hardware initialisation,
// FreeRTOS object creation, task creation, then the scheduler. The application
// lives in the modules (lab step 40):
//
//   sensors       SensorData and SensorTask (DHT22, LDR)
//   display       DisplayMode and DisplayTask (owns the OLED)
//   input         InputTask and the rotary encoder interrupt
//   alarm         AlarmTask
//   motion        MotionTask (PIR) and the ACTIVE/INACTIVE system state
//   rtos_objects  queues, event group and mutex shared between tasks
//   log           serial output shared by every task
//   demo_tasks    tasks from the earlier lab exercises (steps 17-19)
//
// Pure decision logic sits in lib/ rather than src/ so it also builds on the
// host for `pio test -e native` (lab step 42): lib/alarm_logic (temperature
// alarm), lib/display_navigation (encoder page changes) and lib/system_state
// (the ACTIVE/INACTIVE state machine). Device drivers are dht22, ldr and
// ssd1306.

#include "stm32f1xx_hal.h"
#include "alarm.h"
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
//                 Each OLED update busy-waits about 25 ms on I2C; at the bottom
//                 that never holds up the tasks above.
//
// Earlier lab exercises, kept for their demonstrations:
// 3  StateMonitorTask  (step 18) samples SensorTask's state every 500 ms; above
//                      it so SensorTask never delays a sample.
// 2  ProcessingTask    (step 19) continuous work beside SensorTask, blocking
//                      after each batch so the tasks below still run.
// 1  TaskA, TaskB      (step 17) heartbeat prints; nothing depends on them.
#define MOTION_TASK_PRIORITY 3
#define INPUT_TASK_PRIORITY 3
#define SENSOR_TASK_PRIORITY 2
#define ALARM_TASK_PRIORITY 2
#define DISPLAY_TASK_PRIORITY 1
#define MONITOR_TASK_PRIORITY 3
#define PROCESSING_TASK_PRIORITY 2
#define TASK_A_PRIORITY 1
#define TASK_B_PRIORITY 1

// 256 words (1 KB) per task leaves room for the formatting in logPrintf.
#define TASK_STACK_WORDS 256

void SystemClock_Config(void);

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

int main(void)
{
    // 1. Hardware initialisation.
    HAL_Init();
    SystemClock_Config();
    SystemCoreClockUpdate(); // FreeRTOS derives its tick rate from SystemCoreClock
    logInit();
    HAL_StatusTypeDef sensorStatus = sensorsInit();
    motionInit();

    // Direct writes are safe here without serialMutex: the scheduler hasn't
    // started, so nothing else can be using the UART.
    logWriteDirect("BCA182 FreeRTOS Multisensor\r\n");
    logWriteDirect("System starting...\r\n");
    if (sensorStatus != HAL_OK)
    {
        logWriteDirect("Warning: LDR ADC setup or calibration failed\r\n");
    }

    // 2. FreeRTOS object creation.
    if (!createRtosObjects())
    {
        halt("FreeRTOS object creation failed\r\n");
    }
    inputInit(); // enables the encoder interrupt, which sends to encoderQueue, so after it exists

    // 3. Task creation.
    bool tasksCreated =
        createTask(TaskA, "TaskA", TASK_A_PRIORITY) &&
        createTask(TaskB, "TaskB", TASK_B_PRIORITY) &&
        createTask(SensorTask, "Sensor", SENSOR_TASK_PRIORITY, &sensorTaskHandle) &&
        createTask(StateMonitorTask, "Monitor", MONITOR_TASK_PRIORITY) &&
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

// --- Minimal System Clock Configuration defaulting to HSI ---
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}
