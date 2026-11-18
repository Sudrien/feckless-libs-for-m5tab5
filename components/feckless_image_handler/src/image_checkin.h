/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0065: a decoder's check-in with the host, for a picture decoded
 * in software in a feckless box. A box's window holds interrupts off on its
 * core until the app's next host call; a big PNG, or a picture fitted to the
 * screen, is hundreds of milliseconds of that. feckless_box_libc's
 * feckless_box_checkin() makes a host call every 16 K of work; anywhere
 * else -- a launcher, a trusted app -- it is image_decode.c's weak no-op.
 */
#pragma once

#include <stdint.h>

void feckless_box_checkin(uint32_t work);
