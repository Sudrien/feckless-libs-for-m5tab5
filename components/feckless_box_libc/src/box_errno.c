/* SPDX-License-Identifier: MIT
 *
 * errno, the app's own. feckless_abi compiles every component with
 * -D__PICOLIBC_ERRNO_FUNCTION=__errno, so `errno` is (*__errno()); in a boxed
 * app this definition binds first (-Bsymbolic), and errno is a word in the
 * app's own data, not the host's.
 *
 * u-feckless-0023: one per thread. feckless_umode starts each of the app's
 * threads, app_main's included, with tp at a block of its own in the box,
 * and the first word of it is that thread's errno. Nothing else in a boxed
 * app uses tp (no TLS: the ELF is a shared object without it). tp is 0 under
 * a launcher from before 0023, which ran one thread: then the one word here.
 */
#include <errno.h>
#include <stdint.h>

static int s_errno;

int *__errno(void)
{
    uintptr_t tp;
    __asm__ volatile("mv %0, tp" : "=r"(tp));
    return tp ? (int *)tp : &s_errno;
}
