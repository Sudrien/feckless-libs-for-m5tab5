/* SPDX-License-Identifier: MIT
 *
 * errno, the app's own. feckless_abi compiles every component with
 * -D__PICOLIBC_ERRNO_FUNCTION=__errno, so `errno` is (*__errno()); in a boxed
 * app this definition binds first (-Bsymbolic), and errno is a word in the
 * app's own data, not the host's. One for the whole app: a boxed app has one
 * thread.
 */
#include <errno.h>

static int s_errno;

int *__errno(void)
{
    return &s_errno;
}
