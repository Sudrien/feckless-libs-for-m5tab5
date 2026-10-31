/* SPDX-License-Identifier: MIT
 *
 * See nvs_handler.h.
 */
#include "nvs_handler.h"

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
