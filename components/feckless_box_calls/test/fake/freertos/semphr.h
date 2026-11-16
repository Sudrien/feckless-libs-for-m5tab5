#pragma once
#include "freertos/queue.h"
/* u-feckless-0060: a binary semaphore of the kernel's own, as box_camera.c
 * waits on one */
typedef QueueHandle_t SemaphoreHandle_t;
QueueHandle_t xQueueGenericCreate(UBaseType_t len, UBaseType_t item, uint8_t type);
#define xSemaphoreCreateBinary() xQueueGenericCreate(1, 0, queueQUEUE_TYPE_BINARY_SEMAPHORE)
#define xSemaphoreGive(s) xQueueGenericSend((s), NULL, 0, queueSEND_TO_BACK)
#define xSemaphoreTake(s, t) xQueueSemaphoreTake((s), (t))
#define vSemaphoreDelete(s) vQueueDelete(s)
