/* Host stand-in for freertos/task.h, for fetchtest (9003). SPDX-License-Identifier: MIT */
#pragma once
#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
BaseType_t   xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                         UBaseType_t prio, TaskHandle_t *out);
TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                               UBaseType_t prio, StackType_t *stk, StaticTask_t *tcb);
void         vTaskDelete(TaskHandle_t t);
void         vTaskDelay(TickType_t ticks);
uint32_t     ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
BaseType_t   xTaskNotifyGive(TaskHandle_t t);
UBaseType_t  uxTaskGetStackHighWaterMark(TaskHandle_t t);
