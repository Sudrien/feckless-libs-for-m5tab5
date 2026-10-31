/* SPDX-License-Identifier: MIT
 *
 * See feckless_abi_image.h. The table, g_feckless_abi_image_elfsyms, is
 * generated from feckless_abi's sections/image.txt; feckless_abi_run()
 * finds it through a weak reference and registers it beside the core
 * table.
 */
#include "feckless_abi_image.h"

const uint32_t FECKLESS_ABI_IMAGE_SYMBOL = FECKLESS_ABI_IMAGE_VERSION;
