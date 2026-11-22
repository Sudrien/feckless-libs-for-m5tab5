#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_8BIT     (1 << 2)
/* The stand-in's staging buffer is ordinary heap memory, outside the box. */
static inline void *heap_caps_aligned_alloc(size_t align, size_t n, unsigned caps)
{
    (void)caps;
    return aligned_alloc(align, (n + align - 1) / align * align);
}
/* u-feckless-0045: box_rtos.c's control blocks and storage. */
#define MALLOC_CAP_SPIRAM   (1 << 10)
extern unsigned fake_caps_last;
static inline void *heap_caps_malloc(size_t n, unsigned caps) { fake_caps_last = caps; return malloc(n); }
static inline void heap_caps_free(void *p) { free(p); }
/* u-feckless-0082: the launcher's heap by capability, as the test sets it */
#include <stddef.h>
#include <stdint.h>
typedef struct {
    size_t total_free_bytes, total_allocated_bytes, largest_free_block, minimum_free_bytes;
    size_t allocated_blocks, free_blocks, total_blocks;
} multi_heap_info_t;
extern multi_heap_info_t fake_heap_info;
extern unsigned fake_heap_caps_asked;
static inline void heap_caps_get_info(multi_heap_info_t *i, uint32_t caps) { fake_heap_caps_asked = caps; *i = fake_heap_info; }
