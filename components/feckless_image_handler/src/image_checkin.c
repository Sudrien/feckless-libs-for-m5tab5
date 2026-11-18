/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0065: see image_checkin.h. Nothing to check in with outside a
 * box; in one, feckless_box_libc's strong definition (box_heap.c, which
 * any app that allocates links) takes this one's place.
 */
#include "image_checkin.h"

__attribute__((weak)) void feckless_box_checkin(uint32_t work)
{
    (void)work;
}
