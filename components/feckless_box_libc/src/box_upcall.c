/* SPDX-License-Identifier: MIT
 *
 * The app's callback loop (u-feckless-0075), run in the app.
 *
 * A box never calls into its app from a host task; an IDF callback the app
 * registered -- an HTTP server's handler, its queued work -- is called by
 * this loop instead, on a thread of the app's own that the host spawns the
 * first time something needs it (feckless_box_calls' box_upcall.c). Until
 * then nothing runs: the constructor only says where the loop is, in one
 * host call before app_main.
 *
 * The loop asks the host for a call -- a function of the app's and four
 * words -- makes it, and hands back what it returned with its next ask.
 * The function is the app's own code, called here as the app calls it:
 * fewer arguments than four are ignored by the RISC-V convention, and a
 * function returning nothing returns whatever a0 held, which the host's
 * caller ignores. When the app stops, the ask answers 0 and the thread
 * ends.
 *
 * Linked into every ELF that allocates (box_heap.c refers to the loop, so
 * the linker takes this file with it); an ELF that does not has no loop,
 * and a host call that needs callbacks fails with a line saying so.
 */
#include <stdint.h>

typedef struct {
    uint32_t fn;
    uint32_t a[4];
} box_upcall_t;

/* The host's (feckless_abi's box section). */
uint32_t feckless_box_upcall_next(box_upcall_t *u, uint32_t ret);
void feckless_box_upcall_loop_is(void (*loop)(void *));

typedef uint32_t (*box_upcall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);

void feckless_box_upcall_loop(void *arg)
{
    (void)arg;
    box_upcall_t u;
    uint32_t ret = 0;
    while (feckless_box_upcall_next(&u, ret)) {
        ret = ((box_upcall_fn_t)(uintptr_t)u.fn)(u.a[0], u.a[1], u.a[2], u.a[3]);
    }
}

__attribute__((constructor)) static void box_upcall_announce(void)
{
    feckless_box_upcall_loop_is(feckless_box_upcall_loop);
}
