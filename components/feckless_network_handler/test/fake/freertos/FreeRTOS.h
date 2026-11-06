/*
 * Host stand-in for FreeRTOS, for fetchtest (9003): tasks are pthreads,
 * queues and semaphores are a mutex and a condition each. Ticks are
 * milliseconds, and vTaskDelay() runs a hundred times fast, so 8057's
 * 1, 3 and 10 s retry waits take a tenth of a second between them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef unsigned UBaseType_t;
typedef uint8_t  StackType_t;
typedef struct { int unused; } StaticTask_t;
typedef struct fake_task  *TaskHandle_t;
typedef struct fake_queue *QueueHandle_t;
typedef struct fake_queue *SemaphoreHandle_t;
#define pdTRUE            1
#define pdFALSE           0
#define pdPASS            1
#define portMAX_DELAY     0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
