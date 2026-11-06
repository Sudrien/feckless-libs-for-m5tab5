/* Host stand-in for esp_heap_caps.h, for fetchtest (9003): malloc, with
 * a switch to make the next allocations fail. SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM   (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
#define MALLOC_CAP_8BIT     (1u << 2)
void  *heap_caps_malloc(size_t n, uint32_t caps);
void  *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void   heap_caps_free(void *p);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
