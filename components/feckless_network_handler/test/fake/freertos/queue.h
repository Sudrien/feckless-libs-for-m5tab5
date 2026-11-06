/* Host stand-in for freertos/queue.h, for fetchtest (9003). SPDX-License-Identifier: MIT */
#pragma once
#include "freertos/FreeRTOS.h"
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size);
BaseType_t    xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t    xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
BaseType_t    xQueueReset(QueueHandle_t q);
