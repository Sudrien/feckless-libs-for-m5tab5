/* SPDX-License-Identifier: MIT
 * One namespace/key pair's blob in RAM, for mailtest: feckless_nvs_handler's
 * calls as mailcfg.c makes them. */
#include <string.h>
#include "nvs_handler.h"

static char s_ns[16], s_key[16];
static unsigned char s_blob[1024];
static size_t s_len;
int fake_nvs_writes;

esp_err_t nvsh_get_blob(const char *ns, const char *key, void *buf, size_t *len)
{
    if (!s_len || strcmp(ns, s_ns) || strcmp(key, s_key)) return 0x1102;
    if (*len < s_len) return 0x1105;
    memcpy(buf, s_blob, s_len);
    *len = s_len;
    return ESP_OK;
}
esp_err_t nvsh_set_blob(const char *ns, const char *key, const void *buf, size_t len)
{
    if (len > sizeof s_blob || strlen(ns) > 15 || strlen(key) > 15) return ESP_ERR_INVALID_ARG;
    strcpy(s_ns, ns); strcpy(s_key, key);
    memcpy(s_blob, buf, len);
    s_len = len;
    fake_nvs_writes++;
    return ESP_OK;
}
esp_err_t nvsh_erase_key(const char *ns, const char *key)
{
    if (!s_len || strcmp(ns, s_ns) || strcmp(key, s_key)) return 0x1102;
    s_len = 0;
    return ESP_OK;
}
const unsigned char *fake_nvs_blob(size_t *len) { *len = s_len; return s_blob; }
