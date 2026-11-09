/* SPDX-License-Identifier: MIT
 *
 * vTaskDelete() for a boxed app (u-feckless-0023). vTaskDelete(NULL) ends the
 * calling thread, as returning from its function does: feckless_umode's
 * SYS_EXIT, status 0. Deleting another thread is not offered -- the host
 * could not stop it mid-window -- and a handle is ignored, as a thread asked
 * to end another lives on in IDF too if the handle is stale.
 */
#include <stdint.h>

#define SYS_EXIT 4u     /* feckless_umode_sys.h's FECKLESS_SYS_EXIT */

void vTaskDelete(void *task)
{
    if (task) {
        return;
    }
    register uint32_t a0 __asm__("a0") = 0;
    register uint32_t a7 __asm__("a7") = SYS_EXIT;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
    __builtin_unreachable();
}
