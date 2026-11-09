/* SPDX-License-Identifier: MIT
 *
 * heap_caps_* for a boxed app (u-feckless-0020). A box has one heap, its
 * own, and nothing else it may touch: every capability -- SPIRAM, INTERNAL,
 * DMA -- is the box's PSRAM. The app's buffers never reach a DMA engine
 * directly; a host call that needs one copies through its own staging, as
 * feckless_box_calls' reads do. So the caps are accepted and ignored, and
 * these are the box's malloc and free (feckless_umode's heap calls).
 *
 * heap_caps_aligned_alloc is not here: the box's free would not know the
 * offset. A boxed app that wants one gets an import the box does not
 * answer, and the load says so.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    return calloc(n, size);
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    (void)caps;
    return realloc(ptr, size);
}

void heap_caps_free(void *ptr)
{
    free(ptr);
}
