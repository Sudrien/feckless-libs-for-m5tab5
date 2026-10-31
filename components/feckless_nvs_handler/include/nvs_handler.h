/*
 * nvs_handler.h -- settings and saves in the Tab5's NVS partition, a key
 * at a time.
 *
 * ESP-IDF's NVS behind one call per value: each opens the namespace,
 * reads or writes, commits and closes. No handle crosses the API, so an
 * app's ELF can take these from a launcher (the feckless ABI's nvs
 * section, feckless_abi_nvs) with nothing laid out by either side's
 * build but plain integers and bytes.
 *
 * nvsh_init() brings the partition up, once, and is the launcher's (or a
 * firmware's own app_main()'s): an app on a launcher finds it up and
 * does not call it. Everything else may be called from any task once it
 * has; before it, or if it failed, each returns ESP_ERR_INVALID_STATE.
 *
 * A namespace is at most 15 characters, a key too (NVS_KEY_NAME_MAX_SIZE
 * less the NUL). The partition is shared by everything the board runs --
 * a launcher and each app it loads -- so an app keeps to a namespace of
 * its own.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the default NVS partition. If it is full or from a newer
 * NVS format, erase it and initialise it afresh, as IDF's examples do --
 * everything saved is lost, which is said in the log. Returns ESP_OK
 * when it is up, at once if it already was; otherwise IDF's error, and
 * nothing will be saved. Not exported to apps. */
esp_err_t nvsh_init(void);

/* True once nvsh_init() has succeeded. */
bool nvsh_ready(void);

/* A blob. *len is the room at buf on the way in and the blob's size on
 * the way out. ESP_ERR_NVS_NOT_FOUND when there is none;
 * ESP_ERR_NVS_INVALID_LENGTH when it is larger than *len, with *len its
 * size. buf may be NULL to ask the size. */
esp_err_t nvsh_get_blob(const char *ns, const char *key, void *buf, size_t *len);
esp_err_t nvsh_set_blob(const char *ns, const char *key, const void *buf, size_t len);

/* A NUL-terminated string; *len as for a blob, counting the NUL. */
esp_err_t nvsh_get_str(const char *ns, const char *key, char *buf, size_t *len);
esp_err_t nvsh_set_str(const char *ns, const char *key, const char *s);

/* Integers. On any error *out is left as it was, so a caller can fill it
 * with the default first. */
esp_err_t nvsh_get_u8(const char *ns, const char *key, uint8_t *out);
esp_err_t nvsh_set_u8(const char *ns, const char *key, uint8_t v);
esp_err_t nvsh_get_u32(const char *ns, const char *key, uint32_t *out);
esp_err_t nvsh_set_u32(const char *ns, const char *key, uint32_t v);
esp_err_t nvsh_get_i32(const char *ns, const char *key, int32_t *out);
esp_err_t nvsh_set_i32(const char *ns, const char *key, int32_t v);

/* Remove a key, or every key in the namespace. ESP_ERR_NVS_NOT_FOUND
 * when there was nothing to remove. */
esp_err_t nvsh_erase_key(const char *ns, const char *key);
esp_err_t nvsh_erase_ns(const char *ns);

#ifdef __cplusplus
}
#endif
