/* Host stand-in for freertos/semphr.h, for fetchtest (9003): a semaphore
 * is a queue of empty items, as in FreeRTOS. SPDX-License-Identifier: MIT */
#pragma once
#include "freertos/queue.h"
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t s);
