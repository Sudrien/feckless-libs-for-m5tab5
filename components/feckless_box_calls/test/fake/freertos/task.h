#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
#define tskNO_AFFINITY ((BaseType_t)0x7FFFFFFF)
typedef enum { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;
typedef enum { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;
void vTaskDelay(TickType_t t);
/* u-feckless-0060 */
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                   UBaseType_t prio, TaskHandle_t *out, BaseType_t core);
/* u-feckless-0081 */
#define xTaskCreate(fn, name, stack, arg, prio, out) \
    xTaskCreatePinnedToCore((fn), (name), (stack), (arg), (prio), (out), tskNO_AFFINITY)
void vTaskDelete(TaskHandle_t t);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskPriorityGet(TaskHandle_t t);
void vTaskPrioritySet(TaskHandle_t t, UBaseType_t p);
eTaskState eTaskGetState(TaskHandle_t t);
