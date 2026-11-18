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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "multi_heap.h"

/* u-feckless-0065: feckless_box_checkin(work). A window holds interrupts off
 * on its core until the app's next host call (u-feckless-0017), so code that
 * computes for long without one -- a picture decoded in software -- starves
 * the panel, the other threads of the app on that core, and at length the
 * interrupt watchdog. A loop that may run long calls this with a measure of
 * the work since its last call (pixels, say); every CHECKIN_WORK of it, one
 * host call, the cheapest there is, and the core takes what was pending.
 *
 * Here, in box_heap.c, because a component that wants it in an ELF defines a
 * weak no-op of its own for builds without this library -- a launcher's --
 * and a linker extracts an archive member for a strong definition only when
 * the member is wanted for something else: anything that allocates wants
 * this one. */
#define CHECKIN_WORK (16u * 1024u)
uint32_t xTaskGetTickCount(void);          /* TickType_t; FreeRTOS's header is not the box libc's */
static uint32_t s_checkin_work;

void feckless_box_checkin(uint32_t work)
{
    s_checkin_work += work;
    if (s_checkin_work >= CHECKIN_WORK) {
        s_checkin_work = 0;
        (void)xTaskGetTickCount();
    }
}

/* u-feckless-0048: heap_caps_register_failed_alloc_callback()'s, called by
 * these when the box's heap says no -- the app's own code, run in the app. */
typedef void (*box_alloc_failed_t)(size_t size, uint32_t caps, const char *function_name);
static box_alloc_failed_t s_failed;

static void *failed(void *p, size_t size, uint32_t caps, const char *fn)
{
    if (!p && size && s_failed) s_failed(size, caps, fn);
    return p;
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    return failed(malloc(size), size, caps, __func__);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    return failed(calloc(n, size), n * size, caps, __func__);
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    return failed(realloc(ptr, size), size, caps, __func__);
}

void heap_caps_free(void *ptr)
{
    free(ptr);
}

/* ---- u-feckless-0048: what the heap looks like ---------------------------
 * The box's own heap, whatever the caps: free bytes, the largest block one
 * allocation could take, the least free there has been. One host call
 * (feckless_umode's heap_stats) answers them all. */

typedef struct {
    uint32_t total, free, largest, min_free, blocks, free_blocks;
} box_heap_stats_t;

int feckless_box_heap_stats(box_heap_stats_t *out);

static box_heap_stats_t stats(void)
{
    box_heap_stats_t st;
    memset(&st, 0, sizeof st);
    feckless_box_heap_stats(&st);
    return st;
}

size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return stats().free; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return stats().min_free; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return stats().largest; }
size_t heap_caps_get_total_size(uint32_t caps) { (void)caps; return stats().total; }

void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps)
{
    (void)caps;
    const box_heap_stats_t st = stats();
    memset(info, 0, sizeof *info);
    info->total_free_bytes = st.free;
    info->total_allocated_bytes = st.total - st.free;
    info->largest_free_block = st.largest;
    info->minimum_free_bytes = st.min_free;
    info->allocated_blocks = st.blocks;
    info->free_blocks = st.free_blocks;
    info->total_blocks = st.blocks + st.free_blocks;
}

void heap_caps_print_heap_info(uint32_t caps)
{
    (void)caps;
    const box_heap_stats_t st = stats();
    printf("Heap summary for the box:\n");
    printf("  %u bytes free, %u allocated in %u blocks; largest free block %u; least free %u\n",
           (unsigned)st.free, (unsigned)(st.total - st.free), (unsigned)st.blocks,
           (unsigned)st.largest, (unsigned)st.min_free);
}

esp_err_t heap_caps_register_failed_alloc_callback(box_alloc_failed_t cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    s_failed = cb;
    return ESP_OK;
}
