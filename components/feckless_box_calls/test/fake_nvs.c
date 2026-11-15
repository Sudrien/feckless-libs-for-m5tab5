/* SPDX-License-Identifier: MIT
 * u-feckless-0053: a stand-in NVS: 32 entries of (namespace, key, bytes),
 * every value kept as bytes; strings with their NUL. Records the last
 * pointer it was handed to write from, so the test can see it was the
 * host's. */
#include <string.h>
#include "nvs_handler.h"

typedef struct { char ns[16], key[16]; unsigned char v[9000]; size_t n; int used; } ent_t;
static ent_t e_[32];
const void *n_last_src;
int n_writes;

static ent_t *find(const char *ns, const char *key)
{
    for (int i = 0; i < 32; i++) if (e_[i].used && !strcmp(e_[i].ns, ns) && !strcmp(e_[i].key, key)) return &e_[i];
    return NULL;
}
static esp_err_t put(const char *ns, const char *key, const void *v, size_t n)
{
    ent_t *x = find(ns, key);
    for (int i = 0; !x && i < 32; i++) if (!e_[i].used) x = &e_[i];
    if (!x) return ESP_ERR_NO_MEM;
    strcpy(x->ns, ns); strcpy(x->key, key);
    memcpy(x->v, v, n); x->n = n; x->used = 1;
    n_last_src = v; n_writes++;
    return ESP_OK;
}
static esp_err_t get(const char *ns, const char *key, void *buf, size_t *len, size_t fixed)
{
    ent_t *x = find(ns, key);
    if (!x) return ESP_ERR_NVS_NOT_FOUND;
    if (fixed) { if (x->n != fixed) return ESP_ERR_NVS_TYPE_MISMATCH; memcpy(buf, x->v, fixed); return ESP_OK; }
    if (!buf) { *len = x->n; return ESP_OK; }
    if (*len < x->n) { *len = x->n; return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(buf, x->v, x->n); *len = x->n;
    return ESP_OK;
}
bool nvsh_ready(void) { return true; }
esp_err_t nvsh_get_blob(const char *ns, const char *k, void *b, size_t *l) { return get(ns, k, b, l, 0); }
esp_err_t nvsh_get_str(const char *ns, const char *k, char *b, size_t *l) { return get(ns, k, b, l, 0); }
esp_err_t nvsh_set_blob(const char *ns, const char *k, const void *b, size_t l) { return put(ns, k, b, l); }
esp_err_t nvsh_set_str(const char *ns, const char *k, const char *s) { return put(ns, k, s, strlen(s) + 1); }
esp_err_t nvsh_get_u8(const char *ns, const char *k, uint8_t *o) { return get(ns, k, o, NULL, 1); }
esp_err_t nvsh_get_u32(const char *ns, const char *k, uint32_t *o) { return get(ns, k, o, NULL, 4); }
esp_err_t nvsh_get_i32(const char *ns, const char *k, int32_t *o) { return get(ns, k, o, NULL, 4); }
esp_err_t nvsh_set_u8(const char *ns, const char *k, uint8_t v) { return put(ns, k, &v, 1); }
esp_err_t nvsh_set_u32(const char *ns, const char *k, uint32_t v) { return put(ns, k, &v, 4); }
esp_err_t nvsh_set_i32(const char *ns, const char *k, int32_t v) { return put(ns, k, &v, 4); }
esp_err_t nvsh_erase_key(const char *ns, const char *k)
{
    ent_t *x = find(ns, k);
    if (!x) return ESP_ERR_NVS_NOT_FOUND;
    x->used = 0;
    return ESP_OK;
}
esp_err_t nvsh_erase_ns(const char *ns)
{
    int any = 0;
    for (int i = 0; i < 32; i++) if (e_[i].used && !strcmp(e_[i].ns, ns)) { e_[i].used = 0; any = 1; }
    return any ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
