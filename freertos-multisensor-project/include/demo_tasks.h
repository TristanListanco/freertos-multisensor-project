#ifndef DEMO_TASKS_H
#define DEMO_TASKS_H

// Tasks from the earlier lab exercises, kept for their demonstrations. None of
// the room monitor depends on them.

// Heartbeat prints (lab step 17).
void TaskA(void *pvParameters);
void TaskB(void *pvParameters);

// Samples SensorTask's state every 500 ms (lab step 18).
void StateMonitorTask(void *pvParameters);

// Continuous work that blocks after each bounded batch (lab step 19).
void ProcessingTask(void *pvParameters);

#endif // DEMO_TASKS_H
