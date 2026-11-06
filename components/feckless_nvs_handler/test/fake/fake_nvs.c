/* In-memory NVS for nvstest.c. SPDX-License-Identifier: MIT */
#include <stdarg.h>
#include <stdio.h>
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
    fake_log_text[0] = '\0';
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

/* ---- 9005: listing ------------------------------------------------------ */

char fake_log_text[1 << 16];
void fake_log_info(const char *fmt, ...)
{
    const size_t o = strlen(fake_log_text);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(fake_log_text + o, sizeof(fake_log_text) - o, fmt, ap);
    va_end(ap);
    const size_t p = strlen(fake_log_text);
    if (p + 1 < sizeof(fake_log_text)) { fake_log_text[p] = '\n'; fake_log_text[p + 1] = '\0'; }
}

struct fake_it { int at; char ns[16]; int any; };
static int s_live_its;
int fake_nvs_live_iterators(void) { return s_live_its; }

static nvs_type_t code(ftype_t t)
{
    switch (t) {
    case T_BLOB: return NVS_TYPE_BLOB;
    case T_STR:  return NVS_TYPE_STR;
    case T_U8:   return NVS_TYPE_U8;
    case T_U32:  return NVS_TYPE_U32;
    default:     return NVS_TYPE_I32;
    }
}

static int next_from(struct fake_it *it, int from)
{
    for (int i = from; i < MAXI; i++)
        if (s_items[i].used && (it->any || !strcmp(s_items[i].ns, it->ns))) return i;
    return -1;
}

esp_err_t nvs_entry_find(const char *part, const char *ns, nvs_type_t type, nvs_iterator_t *out)
{
    (void)type;
    if (!part || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    struct fake_it *it = calloc(1, sizeof(*it));
    it->any = ns == NULL;
    if (ns) { strncpy(it->ns, ns, 15); }
    it->at = next_from(it, 0);
    if (it->at < 0) { free(it); return ESP_ERR_NVS_NOT_FOUND; }
    s_live_its++;
    *out = it;
    return ESP_OK;
}

esp_err_t nvs_entry_next(nvs_iterator_t *pit)
{
    struct fake_it *it = *pit;
    it->at = next_from(it, it->at + 1);
    if (it->at < 0) { free(it); s_live_its--; *pit = NULL; return ESP_ERR_NVS_NOT_FOUND; }
    return ESP_OK;
}

esp_err_t nvs_entry_info(const nvs_iterator_t it, nvs_entry_info_t *out)
{
    if (!it || !out) return ESP_ERR_INVALID_ARG;
    const item_t *x = &s_items[it->at];
    memset(out, 0, sizeof(*out));
    strncpy(out->namespace_name, x->ns, 15);
    strncpy(out->key, x->key, 15);
    out->type = code(x->t);
    return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t it)
{
    if (it) { free(it); s_live_its--; }
}

esp_err_t nvs_get_stats(const char *part, nvs_stats_t *st)
{
    (void)part;
    int used = 0, ns = 0;
    char seen[MAXI][16];
    for (int i = 0; i < MAXI; i++) {
        if (!s_items[i].used) continue;
        used += 1 + (int)((s_items[i].len + 31) / 32);
        int known = 0;
        for (int j = 0; j < ns && !known; j++) known = !strcmp(seen[j], s_items[i].ns);
        if (!known) strcpy(seen[ns++], s_items[i].ns);
    }
    st->used_entries = (size_t)(used + ns);
    st->total_entries = 504;
    st->free_entries = st->total_entries - st->used_entries;
    st->available_entries = st->free_entries > 126 ? st->free_entries - 126 : 0;
    st->namespace_count = (size_t)ns;
    return ESP_OK;
}

esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *o)   { (void)h; (void)k; (void)o; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *o) { (void)h; (void)k; (void)o; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_i16(nvs_handle_t h, const char *k, int16_t *o)  { (void)h; (void)k; (void)o; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_u64(nvs_handle_t h, const char *k, uint64_t *o) { (void)h; (void)k; (void)o; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_i64(nvs_handle_t h, const char *k, int64_t *o)  { (void)h; (void)k; (void)o; return ESP_ERR_NVS_NOT_FOUND; }
