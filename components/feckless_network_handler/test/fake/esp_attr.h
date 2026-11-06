/*
 * Host stand-in for esp_attr.h, for fetchtest (9004): EXT_RAM_BSS_ATTR as
 * IDF defines it -- a section with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY,
 * nothing without -- so a build can count what moves out of .bss.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "sdkconfig.h"
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define EXT_RAM_BSS_ATTR __attribute__((section(".ext_ram.bss")))
#else
#define EXT_RAM_BSS_ATTR
#endif
