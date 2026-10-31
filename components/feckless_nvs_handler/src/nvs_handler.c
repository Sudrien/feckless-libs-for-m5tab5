/* SPDX-License-Identifier: MIT
 *
 * See nvs_handler.h.
 */
#include "nvs_handler.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_handler";

static volatile bool s_ready;

esp_err_t nvsh_init(void)
{
    if (s_ready) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s); erasing it", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s -- nothing will be saved", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

bool nvsh_ready(void) { return s_ready; }

static esp_err_t open_ns(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!ns) return ESP_ERR_INVALID_ARG;
    return nvs_open(ns, mode, h);
}

/* A write: open, the write, commit, close. */
#define WRITE(ns, call)                                           \
    do {                                                          \
        nvs_handle_t h;                                           \
        esp_err_t err = open_ns((ns), NVS_READWRITE, &h);         \
        if (err != ESP_OK) return err;                            \
        err = (call);                                             \
        if (err == ESP_OK) err = nvs_commit(h);                   \
        nvs_close(h);                                             \
        return err;                                               \
    } while (0)

/* A read: open read-only, the read, close. A namespace never written
 * reads as ESP_ERR_NVS_NOT_FOUND from nvs_open(), as a key would. */
#define READ(ns, call)                                            \
    do {                                                          \
        nvs_handle_t h;                                           \
        esp_err_t err = open_ns((ns), NVS_READONLY, &h);          \
        if (err != ESP_OK) return err;                            \
        err = (call);                                             \
        nvs_close(h);                                             \
        return err;                                               \
    } while (0)

esp_err_t nvsh_get_blob(const char *ns, const char *key, void *buf, size_t *len)
{
    if (!len) return ESP_ERR_INVALID_ARG;
    READ(ns, nvs_get_blob(h, key, buf, len));
}

esp_err_t nvsh_set_blob(const char *ns, const char *key, const void *buf, size_t len)
{
    WRITE(ns, nvs_set_blob(h, key, buf, len));
}

esp_err_t nvsh_get_str(const char *ns, const char *key, char *buf, size_t *len)
{
    if (!len) return ESP_ERR_INVALID_ARG;
    READ(ns, nvs_get_str(h, key, buf, len));
}

esp_err_t nvsh_set_str(const char *ns, const char *key, const char *s)
{
    WRITE(ns, nvs_set_str(h, key, s));
}

/* nvs_get_*() writes the out value only on success, so a default the
 * caller put there survives a missing key. */
esp_err_t nvsh_get_u8(const char *ns, const char *key, uint8_t *out)   { READ(ns, nvs_get_u8(h, key, out)); }
esp_err_t nvsh_set_u8(const char *ns, const char *key, uint8_t v)      { WRITE(ns, nvs_set_u8(h, key, v)); }
esp_err_t nvsh_get_u32(const char *ns, const char *key, uint32_t *out) { READ(ns, nvs_get_u32(h, key, out)); }
esp_err_t nvsh_set_u32(const char *ns, const char *key, uint32_t v)    { WRITE(ns, nvs_set_u32(h, key, v)); }
esp_err_t nvsh_get_i32(const char *ns, const char *key, int32_t *out)  { READ(ns, nvs_get_i32(h, key, out)); }
esp_err_t nvsh_set_i32(const char *ns, const char *key, int32_t v)     { WRITE(ns, nvs_set_i32(h, key, v)); }

esp_err_t nvsh_erase_key(const char *ns, const char *key)
{
    WRITE(ns, nvs_erase_key(h, key));
}

esp_err_t nvsh_erase_ns(const char *ns)
{
    WRITE(ns, nvs_erase_all(h));
}

/* ---- migration (7001) -------------------------------------------------- */

/* Is there a value of this key, as this type? ESP_ERR_NVS_NOT_FOUND is
 * no; ESP_ERR_NVS_TYPE_MISMATCH is a value of another type. *len is its
 * size for a blob or a string, the size query being how NVS says so
 * without reading it. */
static esp_err_t probe(const char *ns, const char *key, nvsh_type_t type, size_t *len)
{
    uint8_t u8;
    uint32_t u32;
    int32_t i32;
    *len = 0;
    switch (type) {
    case NVSH_BLOB: return nvsh_get_blob(ns, key, NULL, len);
    case NVSH_STR:  return nvsh_get_str(ns, key, NULL, len);
    case NVSH_U8:   return nvsh_get_u8(ns, key, &u8);
    case NVSH_U32:  return nvsh_get_u32(ns, key, &u32);
    case NVSH_I32:  return nvsh_get_i32(ns, key, &i32);
    default:        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t copy_one(const char *from, const char *to, const nvsh_move_t *m, size_t len)
{
    esp_err_t err;
    switch (m->type) {
    case NVSH_U8: {
        uint8_t v;
        err = nvsh_get_u8(from, m->key, &v);
        return err == ESP_OK ? nvsh_set_u8(to, m->key, v) : err;
    }
    case NVSH_U32: {
        uint32_t v;
        err = nvsh_get_u32(from, m->key, &v);
        return err == ESP_OK ? nvsh_set_u32(to, m->key, v) : err;
    }
    case NVSH_I32: {
        int32_t v;
        err = nvsh_get_i32(from, m->key, &v);
        return err == ESP_OK ? nvsh_set_i32(to, m->key, v) : err;
    }
    default:
        break;
    }
    void *buf = heap_caps_malloc(len ? len : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(len ? len : 1, MALLOC_CAP_8BIT);
    if (!buf) return ESP_ERR_NO_MEM;
    if (m->type == NVSH_STR) {
        err = nvsh_get_str(from, m->key, buf, &len);
        if (err == ESP_OK) err = nvsh_set_str(to, m->key, buf);
    } else {
        err = nvsh_get_blob(from, m->key, buf, &len);
        if (err == ESP_OK) err = nvsh_set_blob(to, m->key, buf, len);
    }
    heap_caps_free(buf);
    return err;
}

/* One old namespace: the moves from index `first` on that name it. */
static esp_err_t migrate_ns(const char *to, const nvsh_move_t *mv, size_t first, size_t n,
                            size_t *moved)
{
    const char *from = mv[first].ns;
    size_t copied = 0, kept = 0;
    bool found = false;
    esp_err_t result = ESP_OK;

    for (size_t i = first; i < n && result == ESP_OK; i++) {
        if (strcmp(mv[i].ns, from) != 0) continue;
        size_t len;
        esp_err_t err = probe(from, mv[i].key, mv[i].type, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        found = true;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s/%s: %s; %s left as it is", from, mv[i].key,
                     esp_err_to_name(err), from);
            result = err;
            break;
        }
        size_t dlen;
        err = probe(to, mv[i].key, mv[i].type, &dlen);
        if (err == ESP_OK || err == ESP_ERR_NVS_TYPE_MISMATCH) {
            kept++;                     /* the new one is the newer write */
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = copy_one(from, to, &mv[i], len);
            if (err == ESP_OK) {
                copied++;
            } else {
                ESP_LOGE(TAG, "%s/%s not copied (%s); %s left in place", from,
                         mv[i].key, esp_err_to_name(err), from);
                result = err;
            }
        } else {
            result = err;
        }
    }
    *moved += copied;
    if (!found || result != ESP_OK) return result;

    result = nvsh_erase_ns(from);       /* NOT_FOUND cannot be: it had a key */
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "%s copied but not erased: %s", from, esp_err_to_name(result));
        return result;
    }
    ESP_LOGW(TAG, "moved %u value%s from \"%s\" into \"%s\"%s", (unsigned)copied,
             copied == 1 ? "" : "s", from, to,
             kept ? " (some already there, kept)" : "");
    return ESP_OK;
}

esp_err_t nvsh_migrate(const char *to_ns, const nvsh_move_t *moves, size_t n, size_t *moved)
{
    size_t total = 0;
    if (moved) *moved = 0;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!to_ns || (n && !moves)) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < n; i++) {
        if (!moves[i].ns || !moves[i].key || strcmp(moves[i].ns, to_ns) == 0) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    esp_err_t first_err = ESP_OK;
    for (size_t i = 0; i < n; i++) {
        bool seen = false;              /* an earlier entry took this namespace */
        for (size_t j = 0; j < i && !seen; j++) seen = strcmp(moves[j].ns, moves[i].ns) == 0;
        if (seen) continue;
        const esp_err_t err = migrate_ns(to_ns, moves, i, n, &total);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }
    if (moved) *moved = total;
    return first_err;
}
