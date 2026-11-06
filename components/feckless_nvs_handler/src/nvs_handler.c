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

/* ---- what is there (9005) ---------------------------------------------- */

static uint32_t fnv32(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* Whether p[0, n) is text: valid UTF-8 (no overlong forms, no
 * surrogates, nothing past U+10FFFF) with no control character but tab
 * and newline. */
static bool is_text(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ) {
        const uint8_t c = p[i];
        if (c < 0x80) {
            if ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7F) return false;
            i++;
            continue;
        }
        size_t k;
        uint32_t cp;
        if      ((c & 0xE0) == 0xC0) { k = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { k = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { k = 3; cp = c & 0x07; }
        else return false;
        for (size_t j = 1; j <= k; j++) {
            if (i + j >= n || (p[i + j] & 0xC0) != 0x80) return false;
            cp = cp << 6 | (p[i + j] & 0x3F);
        }
        static const uint32_t min[4] = { 0, 0x80, 0x800, 0x10000 };
        if (cp < min[k] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x80 && cp < 0xA0) return false;      /* C1 controls */
        i += k + 1;
    }
    return true;
}

/* A string's or a blob's size, its bytes into a buffer of the caller's
 * to wipe and free (*buf), and their hash (a string's without its NUL). */
static void sized(nvs_handle_t h, const char *key, bool str, nvsh_entry_t *e, uint8_t **buf)
{
    size_t n = 0;
    const esp_err_t q = str ? nvs_get_str(h, key, NULL, &n) : nvs_get_blob(h, key, NULL, &n);
    if (q != ESP_OK) return;
    e->size = (uint32_t)n;
    /* PSRAM first, as copy_one() does: internal RAM is the radio's. */
    uint8_t *b = heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b) b = heap_caps_malloc(n ? n : 1, MALLOC_CAP_8BIT);
    if (!b) return;
    const esp_err_t r = str ? nvs_get_str(h, key, (char *)b, &n) : nvs_get_blob(h, key, b, &n);
    if (r != ESP_OK) { heap_caps_free(b); return; }
    *buf = b;
    e->data = b;
    e->len  = (uint32_t)(str && n > 0 ? n - 1 : n);
    e->hash = fnv32(b, e->len);
    e->text = is_text(b, e->len);
}

static void read_entry(const nvs_entry_info_t *in, nvsh_entry_t *e, uint8_t **buf)
{
    memset(e, 0, sizeof(*e));
    *buf = NULL;
    memcpy(e->ns, in->namespace_name, sizeof(e->ns) - 1);
    memcpy(e->key, in->key, sizeof(e->key) - 1);
    nvs_handle_t h;
    if (nvs_open(e->ns, NVS_READONLY, &h) != ESP_OK) {
        memcpy(e->type, "?", 2);
        return;
    }
#define INT(code, name, ctype, getter)                                      \
    case code: {                                                            \
        ctype v = 0;                                                        \
        memcpy(e->type, name, sizeof(name));                                \
        e->size = sizeof(ctype);                                            \
        if (getter(h, e->key, &v) == ESP_OK) e->value = (int64_t)v;         \
        break;                                                              \
    }
    switch (in->type) {
    INT(NVS_TYPE_U8,  "u8",  uint8_t,  nvs_get_u8)
    INT(NVS_TYPE_I8,  "i8",  int8_t,   nvs_get_i8)
    INT(NVS_TYPE_U16, "u16", uint16_t, nvs_get_u16)
    INT(NVS_TYPE_I16, "i16", int16_t,  nvs_get_i16)
    INT(NVS_TYPE_U32, "u32", uint32_t, nvs_get_u32)
    INT(NVS_TYPE_I32, "i32", int32_t,  nvs_get_i32)
    INT(NVS_TYPE_U64, "u64", uint64_t, nvs_get_u64)
    INT(NVS_TYPE_I64, "i64", int64_t,  nvs_get_i64)
    case NVS_TYPE_STR:  memcpy(e->type, "str", 4);  sized(h, e->key, true, e, buf);  break;
    case NVS_TYPE_BLOB: memcpy(e->type, "blob", 5); sized(h, e->key, false, e, buf); break;
    default:            memcpy(e->type, "?", 2);    break;
    }
#undef INT
    nvs_close(h);
}

int nvsh_list(const char *ns, nvsh_entry_cb_t cb, void *arg)
{
    if (!s_ready) return -1;
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_INVALID_ARG) return -1;      /* and nothing to release */
    int n = 0;
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        if (nvs_entry_info(it, &info) != ESP_OK) { err = ESP_FAIL; break; }
        nvsh_entry_t e;
        uint8_t *buf;
        read_entry(&info, &e, &buf);
        n++;
        const bool go = !cb || cb(&e, arg);
        if (buf) {                  /* nothing of the bytes outlives the call */
            memset(buf, 0, e.size);
            heap_caps_free(buf);
        }
        if (!go) break;
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) ? n : -1;
}

esp_err_t nvsh_stats(nvsh_stats_t *out)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!out) return ESP_ERR_INVALID_ARG;
    nvs_stats_t st;
    const esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &st);
    if (err != ESP_OK) return err;
    out->used       = (uint32_t)st.used_entries;
    out->free       = (uint32_t)st.free_entries;
    out->available  = (uint32_t)st.available_entries;
    out->total      = (uint32_t)st.total_entries;
    out->namespaces = (uint32_t)st.namespace_count;
    return ESP_OK;
}

#define LOG_TEXT_MAX (160)

/* Text for one line: quoted, \n \t \" \\ escaped, the first
 * LOG_TEXT_MAX bytes cut on a character boundary, "..." after a cut. */
static void quote(const nvsh_entry_t *e, char *out, size_t cap)
{
    size_t o = 0;
    out[o++] = '"';
    uint32_t i = 0;
    for (; i < e->len && o + 8 < cap && i < LOG_TEXT_MAX; ) {
        const uint8_t c = e->data[i];
        if (c == '\n' || c == '\t' || c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c == '\n' ? 'n' : c == '\t' ? 't' : (char)c;
            i++;
            continue;
        }
        /* A whole character, or none of it. */
        const uint32_t k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (o + k + 6 >= cap || i + k > LOG_TEXT_MAX) break;
        memcpy(out + o, e->data + i, k);
        o += k;
        i += k;
    }
    out[o++] = '"';
    if (i < e->len) { memcpy(out + o, "...", 3); o += 3; }
    out[o] = '\0';
}

static bool log_one(const nvsh_entry_t *e, void *arg)
{
    const bool contents = arg != NULL;
    if (e->type[0] == 's' || e->type[0] == 'b') {
        if (contents && e->text && e->data) {
            char q[LOG_TEXT_MAX * 2 + 16];
            quote(e, q, sizeof(q));
            ESP_LOGI(TAG, "  %-15s %-15s %-4s %lu bytes, #%08lx %s", e->ns, e->key, e->type,
                     (unsigned long)e->size, (unsigned long)e->hash, q);
        } else {
            ESP_LOGI(TAG, "  %-15s %-15s %-4s %lu bytes, #%08lx", e->ns, e->key, e->type,
                     (unsigned long)e->size, (unsigned long)e->hash);
        }
    } else if (e->type[0] == 'u') {
        ESP_LOGI(TAG, "  %-15s %-15s %-4s %llu", e->ns, e->key, e->type,
                 (unsigned long long)(uint64_t)e->value);
    } else {
        ESP_LOGI(TAG, "  %-15s %-15s %-4s %lld", e->ns, e->key, e->type, (long long)e->value);
    }
    return true;
}

void nvsh_log(const char *ns, bool contents)
{
    nvsh_stats_t st;
    if (nvsh_stats(&st) == ESP_OK) {
        ESP_LOGI(TAG, "NVS: %lu of %lu entries used, %lu free (%lu for data), %lu namespaces",
                 (unsigned long)st.used, (unsigned long)st.total, (unsigned long)st.free,
                 (unsigned long)st.available, (unsigned long)st.namespaces);
    }
    if (contents) ESP_LOGW(TAG, "NVS contents follow, secrets included: do not share this log");
    static char yes = 1;        /* any non-NULL arg: show text */
    const int n = nvsh_list(ns, log_one, contents ? &yes : NULL);
    if (n < 0) ESP_LOGW(TAG, "NVS could not be listed");
    else       ESP_LOGI(TAG, "NVS: %d key(s)%s%s", n, ns ? " in " : "", ns ? ns : "");
}
