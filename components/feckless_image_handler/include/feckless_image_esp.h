/*
 * feckless_image_esp.h -- the ESP-IDF backends for img_decode() (7008).
 *
 *   img_esp_init();                                  once, at boot
 *   img_decode(img_esp_ops(), buf, len, w, h, bg, &pic);
 *
 * Fills img_ops_t with: the P4's hardware JPEG engine (esp_driver_jpeg),
 * TJpgDec through espressif/esp_jpeg, and stb_image, pngle and J40 on
 * big PSRAM stacks, all at idle priority; and heap_caps for the figures.
 * Moved from Defeatist's albumart.c with its reasons (1104, 5055, 5062,
 * 5093, 6147).
 *
 * BUILD REQUIREMENT: CONFIG_JD_USE_SCALE=y (TJpgDec's 1/2..1/8 reduction;
 * without it esp_jpeg answers JDR_PAR). CONFIG_JD_FASTDECODE may be
 * anything; the work buffer is supplied here, so its size does not move.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "feckless_image_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Makes the hardware engine and keeps it for the session, as Defeatist's
 * albumart_init() did: it is an allocation that can fail late. Without
 * it (or when the chip has none) hw_ready is 0 and JPEGs go to software.
 * Safe to call twice. */
void img_esp_init(void);

const img_ops_t *img_esp_ops(void);

#ifdef __cplusplus
}
#endif
