/* SPDX-License-Identifier: MIT
 *
 * NVS for a boxed app (u-feckless-0053): feckless_nvs_handler's values,
 * strings and blobs, in the namespaces the launcher gave this app and no
 * other.
 *
 * NVS is the launcher's too -- its Wi-Fi credentials, its own settings,
 * every other app's -- so a box answers a namespace only if the launcher
 * named it with feckless_box_calls_nvs() before the load. Named nothing,
 * the app has no NVS: a get is ESP_ERR_NVS_NOT_FOUND, as on a device whose
 * NVS is empty, and a set or erase ESP_ERR_NOT_ALLOWED. The names go with
 * the app (feckless_box_calls_end()), so the next app starts with none.
 *
 * - Every string and blob is copied through the host's heap: what the
 *   host hands NVS is never the app's memory, which another of its threads
 *   could change in the middle of the write; and a read lands in the app
 *   only once NVS has given it all. Blobs and strings are BLOB_MAX at most.
 * - Unlocked: a write to flash takes milliseconds, and the app's other
 *   threads' host calls need not wait for it. The handler's own lock
 *   orders them; the names are only read while an app runs.
 * - feckless_abi_nvs_v1 is answered, so an app can require the section
 *   (abicheck has it do so), as a box answers feckless_abi_storage_v2
 *   without all of storage. Not answered: nvsh_list(), nvsh_log(),
 *   nvsh_stats(), nvsh_migrate() -- they walk or move whole namespaces,
 *   the launcher's among them -- so an app importing one is refused at
 *   load by that name.
 */
#include <string.h>

#include "box_calls_priv.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "nvs.h"                  /* the ESP_ERR_NVS_ codes */
#include "nvs_handler.h"

#define NS_MAX     4
#define NS_LEN     15u                      /* NVS's own limit */
#define KEY_LEN    15u
#define BLOB_MAX   8192u

static char s_ns[NS_MAX][NS_LEN + 1];
static const uint32_t s_nvs_v1 = 1;         /* feckless_abi_nvs_v1 */
static uint32_t s_ns_n;

bool feckless_box_calls_nvs(const char *const *ns, uint32_t n)
{
    if (n > NS_MAX || (n && !ns)) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (!ns[i] || !ns[i][0] || strnlen(ns[i], NS_LEN + 1) > NS_LEN) return false;
    }
    memset(s_ns, 0, sizeof s_ns);
    for (uint32_t i = 0; i < n; i++) memcpy(s_ns[i], ns[i], strlen(ns[i]));
    s_ns_n = n;
    return true;
}

void box_nvs_end(void)
{
    memset(s_ns, 0, sizeof s_ns);
    s_ns_n = 0;
}

/* The app's namespace, if the launcher gave it; NULL if not, or not a
 * string in the box. */
static const char *ns_of(win_t *w, uint32_t p)
{
    const char *ns = str(w, p, NS_LEN + 1);
    if (!ns) return NULL;
    for (uint32_t i = 0; i < s_ns_n; i++) {
        if (strcmp(ns, s_ns[i]) == 0) return s_ns[i];
    }
    return NULL;
}

/* Namespace and key, as the call's first two arguments: ESP_OK, or what
 * to answer. A namespace not given reads as empty and refuses a change. */
static esp_err_t names(win_t *w, const uint32_t a[7], bool change, const char **ns, const char **key)
{
    *key = str(w, a[1], KEY_LEN + 1);
    if (!*key || !(*key)[0]) return ESP_ERR_INVALID_ARG;
    if (!str(w, a[0], NS_LEN + 1)) return ESP_ERR_INVALID_ARG;
    *ns = ns_of(w, a[0]);
    if (!*ns) return change ? ESP_ERR_NOT_ALLOWED : ESP_ERR_NVS_NOT_FOUND;
    return ESP_OK;
}

/* (ns, key, uint8_t/uint32_t/int32_t *out) */
#define GETTER(fn, type)                                                        \
    H(c_##fn)                                                                   \
    {                                                                           \
        const char *ns, *key;                                                   \
        esp_err_t e = names(w, a, false, &ns, &key);                            \
        if (e == ESP_OK && !ok_ptr(w, a[2], sizeof(type))) e = ESP_ERR_INVALID_ARG; \
        if (e == ESP_OK) {                                                      \
            type v = 0;                                                         \
            e = fn(ns, key, &v);                                                \
            if (e == ESP_OK) memcpy((void *)(uintptr_t)a[2], &v, sizeof v);     \
        }                                                                       \
        *ret = (uint32_t)e;                                                     \
        return true;                                                            \
    }
/* (ns, key, value) */
#define SETTER(fn, type)                                                        \
    H(c_##fn)                                                                   \
    {                                                                           \
        const char *ns, *key;                                                   \
        esp_err_t e = names(w, a, true, &ns, &key);                             \
        if (e == ESP_OK) e = fn(ns, key, (type)a[2]);                           \
        *ret = (uint32_t)e;                                                     \
        return true;                                                            \
    }
GETTER(nvsh_get_u8, uint8_t)
GETTER(nvsh_get_u32, uint32_t)
GETTER(nvsh_get_i32, int32_t)
SETTER(nvsh_set_u8, uint8_t)
SETTER(nvsh_set_u32, uint32_t)
SETTER(nvsh_set_i32, int32_t)

/* (ns, key, buf, size_t *len): the handler's two ways -- buf NULL asks the
 * length; buf given reads at most *len. is_str says which call. */
static esp_err_t get_var(win_t *w, const uint32_t a[7], bool is_str)
{
    const char *ns, *key;
    esp_err_t e = names(w, a, false, &ns, &key);
    if (e != ESP_OK) return e;
    if (!ok_ptr(w, a[3], 4)) return ESP_ERR_INVALID_ARG;
    uint32_t cap;
    memcpy(&cap, (const void *)(uintptr_t)a[3], 4);
    size_t len = 0;
    if (!a[2]) {                            /* the length only */
        e = is_str ? nvsh_get_str(ns, key, NULL, &len) : nvsh_get_blob(ns, key, NULL, &len);
        if (e == ESP_OK) {
            const uint32_t l = (uint32_t)len;
            memcpy((void *)(uintptr_t)a[3], &l, 4);
        }
        return e;
    }
    if (!ok_ptr(w, a[2], cap)) return ESP_ERR_INVALID_ARG;
    len = cap < BLOB_MAX ? cap : BLOB_MAX;
    uint8_t *b = heap_caps_malloc(len ? len : 1, MALLOC_CAP_8BIT);
    if (!b) return ESP_ERR_NO_MEM;
    e = is_str ? nvsh_get_str(ns, key, (char *)b, &len) : nvsh_get_blob(ns, key, b, &len);
    if (e == ESP_OK && len <= cap) memcpy((void *)(uintptr_t)a[2], b, len);
    if (e == ESP_OK || e == ESP_ERR_NVS_INVALID_LENGTH) {  /* the size, as the handler says, either way */
        const uint32_t l = (uint32_t)len;
        memcpy((void *)(uintptr_t)a[3], &l, 4);
    }
    heap_caps_free(b);
    return e;
}
H(c_nvsh_get_str) { *ret = (uint32_t)get_var(w, a, true); return true; }
H(c_nvsh_get_blob) { *ret = (uint32_t)get_var(w, a, false); return true; }

/* (ns, key, const char *s) */
H(c_nvsh_set_str)
{
    const char *ns, *key;
    esp_err_t e = names(w, a, true, &ns, &key);
    const int n = e == ESP_OK ? feckless_umode_strlen(w, (uintptr_t)a[2], BLOB_MAX) : -1;
    if (e == ESP_OK && n < 0) e = ESP_ERR_INVALID_ARG;
    if (e == ESP_OK) {
        char *s = heap_caps_malloc((size_t)n + 1, MALLOC_CAP_8BIT);
        if (!s) {
            e = ESP_ERR_NO_MEM;
        } else {
            memcpy(s, (const void *)(uintptr_t)a[2], (size_t)n);
            s[n] = '\0';                    /* as measured, whatever the app has done since */
            e = nvsh_set_str(ns, key, s);
            heap_caps_free(s);
        }
    }
    *ret = (uint32_t)e;
    return true;
}

/* (ns, key, const void *buf, size_t len) */
H(c_nvsh_set_blob)
{
    const char *ns, *key;
    esp_err_t e = names(w, a, true, &ns, &key);
    if (e == ESP_OK && (a[3] > BLOB_MAX || (a[3] && !ok_ptr(w, a[2], a[3])))) e = ESP_ERR_INVALID_ARG;
    if (e == ESP_OK) {
        uint8_t *b = heap_caps_malloc(a[3] ? a[3] : 1, MALLOC_CAP_8BIT);
        if (!b) {
            e = ESP_ERR_NO_MEM;
        } else {
            if (a[3]) memcpy(b, (const void *)(uintptr_t)a[2], a[3]);
            e = nvsh_set_blob(ns, key, b, a[3]);
            heap_caps_free(b);
        }
    }
    *ret = (uint32_t)e;
    return true;
}

H(c_nvsh_erase_key)
{
    const char *ns, *key;
    esp_err_t e = names(w, a, true, &ns, &key);
    if (e == ESP_OK) e = nvsh_erase_key(ns, key);
    *ret = (uint32_t)e;
    return true;
}

/* (ns): one of the app's own, all of it */
H(c_nvsh_erase_ns)
{
    esp_err_t e = ESP_ERR_INVALID_ARG;
    if (str(w, a[0], NS_LEN + 1)) {
        const char *ns = ns_of(w, a[0]);
        e = ns ? nvsh_erase_ns(ns) : ESP_ERR_NOT_ALLOWED;
    }
    *ret = (uint32_t)e;
    return true;
}

/* NVS is up and the app has a namespace in it */
H(c_nvsh_ready) { (void)w; (void)a; *ret = nvsh_ready() && s_ns_n; return true; }

const feckless_umode_call_t box_nvs_calls[] = {
    { "feckless_abi_nvs_v1", NULL, &s_nvs_v1, sizeof s_nvs_v1, 0 },
    FNU(nvsh_get_u8), FNU(nvsh_get_u32), FNU(nvsh_get_i32),
    FNU(nvsh_set_u8), FNU(nvsh_set_u32), FNU(nvsh_set_i32),
    FNU(nvsh_get_str), FNU(nvsh_get_blob), FNU(nvsh_set_str), FNU(nvsh_set_blob),
    FNU(nvsh_erase_key), FNU(nvsh_erase_ns), FNU(nvsh_ready),
};
const uint32_t box_nvs_calls_n = sizeof box_nvs_calls / sizeof box_nvs_calls[0];
