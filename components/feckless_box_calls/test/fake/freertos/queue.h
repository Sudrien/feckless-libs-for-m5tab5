#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
typedef struct fake_q *QueueHandle_t;
typedef struct { uint8_t opaque[80]; } StaticQueue_t;
#define queueSEND_TO_BACK  ((BaseType_t)0)
#define queueSEND_TO_FRONT ((BaseType_t)1)
#define queueOVERWRITE     ((BaseType_t)2)
#define queueQUEUE_TYPE_BASE               ((uint8_t)0U)
#define queueQUEUE_TYPE_MUTEX              ((uint8_t)1U)
#define queueQUEUE_TYPE_COUNTING_SEMAPHORE ((uint8_t)2U)
#define queueQUEUE_TYPE_BINARY_SEMAPHORE   ((uint8_t)3U)
#define queueQUEUE_TYPE_RECURSIVE_MUTEX    ((uint8_t)4U)
QueueHandle_t xQueueGenericCreateStatic(UBaseType_t len, UBaseType_t item, uint8_t *store, StaticQueue_t *ctl, uint8_t type);
QueueHandle_t xQueueCreateMutexStatic(uint8_t type, StaticQueue_t *ctl);
QueueHandle_t xQueueCreateCountingSemaphoreStatic(UBaseType_t max, UBaseType_t init, StaticQueue_t *ctl);
void vQueueDelete(QueueHandle_t q);
/* u-feckless-0083: a queue of items, allocated */
QueueHandle_t xQueueGenericCreate(UBaseType_t len, UBaseType_t item, uint8_t type);
#define xQueueCreate(len, item) xQueueGenericCreate((len), (item), queueQUEUE_TYPE_BASE)
#define xQueueSend(q, item, t) xQueueGenericSend((q), (item), (t), queueSEND_TO_BACK)
BaseType_t xQueueGenericSend(QueueHandle_t q, const void *item, TickType_t t, BaseType_t pos);
BaseType_t xQueueSemaphoreTake(QueueHandle_t q, TickType_t t);
BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t t);
BaseType_t xQueuePeek(QueueHandle_t q, void *buf, TickType_t t);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t newq);
BaseType_t xQueueTakeMutexRecursive(QueueHandle_t q, TickType_t t);
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t q);
