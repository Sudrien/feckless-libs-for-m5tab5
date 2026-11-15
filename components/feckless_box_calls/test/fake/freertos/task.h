#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
#define tskNO_AFFINITY ((BaseType_t)0x7FFFFFFF)
typedef enum { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;
typedef enum { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;
void vTaskDelay(TickType_t t);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskPriorityGet(TaskHandle_t t);
void vTaskPrioritySet(TaskHandle_t t, UBaseType_t p);
eTaskState eTaskGetState(TaskHandle_t t);
