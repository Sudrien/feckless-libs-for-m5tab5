#pragma once
#include <stddef.h>
typedef struct {
    size_t total_free_bytes, total_allocated_bytes, largest_free_block, minimum_free_bytes,
           allocated_blocks, free_blocks, total_blocks;
} multi_heap_info_t;
