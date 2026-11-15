#pragma once
/* The part of FreeRTOS box_calls.c and box_rtos.c (u-feckless-0045) use:
 * the values as the IDF's, the calls single-threaded stand-ins
 * (fake_rtos.c) that never wait. */
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))        /* a 1 kHz tick */
#define configMAX_PRIORITIES 25
#define configMAX_TASK_NAME_LEN 16
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
extern int fake_crit_depth;
#define portENTER_CRITICAL(m) ((void)(m), fake_crit_depth++)
#define portEXIT_CRITICAL(m) ((void)(m), fake_crit_depth--)
BaseType_t xPortGetCoreID(void);
