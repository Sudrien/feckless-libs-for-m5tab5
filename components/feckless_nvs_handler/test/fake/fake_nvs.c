/* In-memory NVS for nvstest.c. SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

int fake_log_warn, fake_log_err;

const char *esp_err_to_name(esp_err_t e) { (void)e; return "err"; }

typedef enum { T_BLOB, T_STR, T_U8, T_U32, T_I32 } ftype_t;
typedef struct { char ns[16], key[16]; ftype_t t; void *data; size_t len; int used; } item_t;
#define MAXI 64
static item_t s_items[MAXI];
static char s_h_ns[8][16];
static int s_h_rw[8], s_h_open[8];
static int s_fail_after = -1, s_writes;

void fake_nvs_reset(void)
{
    for (int i = 0; i < MAXI; i++) { free(s_items[i].data); memset(&s_items[i], 0, sizeof s_items[i]); }
    memset(s_h_open, 0, sizeof s_h_open);
    s_fail_after = -1; s_writes = 0; fake_log_warn = fake_log_err = 0;
}
int fake_nvs_fail_writes_after(int n) { int o = s_fail_after; s_fail_after = n; s_writes = 0; return o; }
int fake_nvs_writes(void) { return s_writes; }
int fake_nvs_has_ns(const char *ns)
{
    for (int i = 0; i < MAXI; i++) if (s_items[i].used && !strcmp(s_items[i].ns, ns)) return 1;
    return 0;
}

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { fake_nvs_reset(); return ESP_OK; }

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (mode == NVS_READONLY && !fake_nvs_has_ns(ns)) return ESP_ERR_NVS_NOT_FOUND;
    for (int i = 0; i < 8; i++) if (!s_h_open[i]) {
        s_h_open[i] = 1; s_h_rw[i] = mode == NVS_READWRITE;
        strncpy(s_h_ns[i], ns, 15); s_h_ns[i][15] = 0; *h = i; return ESP_OK;
    }
    return ESP_FAIL;
}
void nvs_close(nvs_handle_t h) { s_h_open[h] = 0; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

static item_t *find(nvs_handle_t h, const char *k)
{
    for (int i = 0; i < MAXI; i++)
        if (s_items[i].used && !strcmp(s_items[i].ns, s_h_ns[h]) && !strcmp(s_items[i].key, k)) return &s_items[i];
    return NULL;
}

static esp_err_t put(nvs_handle_t h, const char *k, ftype_t t, const void *v, size_t len)
{
    if (!s_h_rw[h]) return ESP_FAIL;
    if (s_fail_after >= 0 && s_writes++ >= s_fail_after) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    item_t *it = find(h, k);
    if (!it) for (int i = 0; i < MAXI && !it; i++) if (!s_items[i].used) it = &s_items[i];
    if (!it) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    free(it->data);
    it->data = malloc(len ? len : 1);
    memcpy(it->data, v, len);
    it->len = len; it->t = t; it->used = 1;
    strncpy(it->ns, s_h_ns[h], 15); strncpy(it->key, k, 15);
    return ESP_OK;
}

static esp_err_t get(nvs_handle_t h, const char *k, ftype_t t, void *out, size_t *len)
{
    item_t *it = find(h, k);
    if (!it) return ESP_ERR_NVS_NOT_FOUND;
    if (it->t != t) return ESP_ERR_NVS_TYPE_MISMATCH;
    if (len) {
        if (!out) { *len = it->len; return ESP_OK; }
        if (*len < it->len) { *len = it->len; return ESP_ERR_NVS_INVALID_LENGTH; }
        *len = it->len;
    }
    if (out) memcpy(out, it->data, it->len);
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
{
    if (!s_h_rw[h]) return ESP_FAIL;
    item_t *it = find(h, k);
    if (!it) return ESP_ERR_NVS_NOT_FOUND;
    free(it->data); memset(it, 0, sizeof *it);
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t h)
{
    if (!s_h_rw[h]) return ESP_FAIL;
    for (int i = 0; i < MAXI; i++)
        if (s_items[i].used && !strcmp(s_items[i].ns, s_h_ns[h])) { free(s_items[i].data); memset(&s_items[i], 0, sizeof s_items[i]); }
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *o, size_t *l) { return get(h, k, T_BLOB, o, l); }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t l) { return put(h, k, T_BLOB, v, l); }
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *o, size_t *l) { return get(h, k, T_STR, o, l); }
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { return put(h, k, T_STR, v, strlen(v) + 1); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *o) { return get(h, k, T_U8, o, NULL); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { return put(h, k, T_U8, &v, 1); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *o) { return get(h, k, T_U32, o, NULL); }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) { return put(h, k, T_U32, &v, 4); }
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *o) { return get(h, k, T_I32, o, NULL); }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) { return put(h, k, T_I32, &v, 4); }
