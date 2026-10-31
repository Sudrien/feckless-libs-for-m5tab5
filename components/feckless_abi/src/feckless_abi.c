/* SPDX-License-Identifier: MIT
 *
 * See feckless_abi.h.
 */
#include "feckless_abi.h"

#include <errno.h>
#include <string.h>

#include "esp_elf.h"
#include "esp_log.h"

static const char *TAG = "feckless_abi";

const uint32_t FECKLESS_ABI_SYMBOL = FECKLESS_ABI_VERSION;

extern esp_elf_symbol_table_t g_feckless_abi_elfsyms[];   /* generated */
const void *const feckless_abi_table = g_feckless_abi_elfsyms;

/* newlib's newer float functions call this, so an ELF built with
 * IDF 6.1's toolchain imports it; 5.5's libm does not have it. Weak, so
 * the table means the same on both. A signalling NaN: exponent all
 * ones, the quiet bit clear, the rest of the mantissa not zero. */
__attribute__((weak)) int __issignalingf(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof u);
    return (u & 0x7fc00000u) == 0x7f800000u && (u & 0x003fffffu) != 0;
}

/* Static: the app's sections are referenced from here while it runs. */
static esp_elf_t s_elf;

int feckless_abi_run(const uint8_t *image, size_t size)
{
    int ret = esp_elf_init(&s_elf);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_elf_init: %d", ret);
        return ret;
    }
    ret = esp_elf_register_symbol(g_feckless_abi_elfsyms);
    if (ret < 0 && ret != -EEXIST) {
        ESP_LOGE(TAG, "esp_elf_register_symbol: %d", ret);
        esp_elf_deinit(&s_elf);
        return ret;
    }
    ret = esp_elf_relocate(&s_elf, image, size);
    if (ret < 0) {
        /* Most often a symbol the app wants and this ABI lacks -- a
         * feckless_abi_vN of another version, or a name not in
         * exports.txt. esp_elf has logged which. */
        ESP_LOGE(TAG, "esp_elf_relocate: %d (ABI v%d)", ret, FECKLESS_ABI_VERSION);
        esp_elf_unregister_symbol(g_feckless_abi_elfsyms);
        esp_elf_deinit(&s_elf);
        return ret;
    }
    ESP_LOGI(TAG, "app: %u bytes, ABI v%d", (unsigned)size, FECKLESS_ABI_VERSION);
    return esp_elf_request(&s_elf, 0, 0, NULL);
}
