/* SPDX-License-Identifier: MIT
 *
 * See feckless_abi.h.
 */
#include "feckless_abi.h"

#include <errno.h>
#include <string.h>

#include "esp_elf.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "driver/uart.h"   /* 6002 */

static const char *TAG = "feckless_abi";

const uint32_t FECKLESS_ABI_SYMBOL = FECKLESS_ABI_VERSION;

extern esp_elf_symbol_table_t g_feckless_abi_elfsyms[];   /* generated */
const void *const feckless_abi_table = g_feckless_abi_elfsyms;

/* 3007: the sections. Each is a component of its own (feckless_abi_<name>)
 * that a launcher links only if it has what the section exports; its
 * table is weak here, so a launcher without it registers nothing for it
 * and an app that needs it fails in esp_elf_relocate(), naming the
 * section's version symbol. */
extern esp_elf_symbol_table_t g_feckless_abi_storage_elfsyms[] __attribute__((weak));
extern esp_elf_symbol_table_t g_feckless_abi_nvs_elfsyms[] __attribute__((weak));      /* 4004 */
extern esp_elf_symbol_table_t g_feckless_abi_camera_elfsyms[] __attribute__((weak));   /* 4005 */
extern esp_elf_symbol_table_t g_feckless_abi_network_elfsyms[] __attribute__((weak));  /* 6004 */

static esp_elf_symbol_table_t *const s_sections[] = {
    g_feckless_abi_storage_elfsyms,
    g_feckless_abi_nvs_elfsyms,
    g_feckless_abi_camera_elfsyms,
    g_feckless_abi_network_elfsyms,
};
#define SECTION_COUNT (sizeof s_sections / sizeof s_sections[0])

static void unregister_all(void)
{
    esp_elf_unregister_symbol(g_feckless_abi_elfsyms);
    for (size_t i = 0; i < SECTION_COUNT; i++) {
        if (s_sections[i]) {
            esp_elf_unregister_symbol(s_sections[i]);
        }
    }
}

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

/* 6002: IDF 6's uart_set_pin() is a macro that calls _uart_set_pin6();
 * 5.5's is a function of four pins. An app built on 6.1 imports the
 * former, so a 5.5 launcher supplies it here. */
#if ESP_IDF_VERSION_MAJOR < 6
esp_err_t _uart_set_pin6(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                         int rts_io_num, int cts_io_num, int dtr_io_num, int dsr_io_num)
{
    (void)dtr_io_num;
    (void)dsr_io_num;
    return uart_set_pin(uart_num, tx_io_num, rx_io_num, rts_io_num, cts_io_num);
}
#endif

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
    for (size_t i = 0; i < SECTION_COUNT; i++) {
        if (!s_sections[i]) {
            continue;
        }
        ret = esp_elf_register_symbol(s_sections[i]);
        if (ret < 0 && ret != -EEXIST) {
            ESP_LOGE(TAG, "esp_elf_register_symbol (section %u): %d", (unsigned)i, ret);
            unregister_all();
            esp_elf_deinit(&s_elf);
            return ret;
        }
    }
    ret = esp_elf_relocate(&s_elf, image, size);
    if (ret < 0) {
        /* Most often a symbol the app wants and this ABI lacks -- a
         * feckless_abi_vN of another version, or a name not in
         * exports.txt. esp_elf has logged which. */
        ESP_LOGE(TAG, "esp_elf_relocate: %d (ABI v%d)", ret, FECKLESS_ABI_VERSION);
        unregister_all();
        esp_elf_deinit(&s_elf);
        return ret;
    }
    ESP_LOGI(TAG, "app: %u bytes, ABI v%d", (unsigned)size, FECKLESS_ABI_VERSION);
    return esp_elf_request(&s_elf, 0, 0, NULL);
}
