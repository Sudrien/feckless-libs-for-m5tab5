#pragma once
/* u-feckless-0053: feckless_nvs_handler's calls box_nvs.c uses */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
bool nvsh_ready(void);
esp_err_t nvsh_get_blob(const char *ns, const char *key, void *buf, size_t *len);
esp_err_t nvsh_set_blob(const char *ns, const char *key, const void *buf, size_t len);
esp_err_t nvsh_get_str(const char *ns, const char *key, char *buf, size_t *len);
esp_err_t nvsh_set_str(const char *ns, const char *key, const char *s);
esp_err_t nvsh_get_u8(const char *ns, const char *key, uint8_t *out);
esp_err_t nvsh_set_u8(const char *ns, const char *key, uint8_t v);
esp_err_t nvsh_get_u32(const char *ns, const char *key, uint32_t *out);
esp_err_t nvsh_set_u32(const char *ns, const char *key, uint32_t v);
esp_err_t nvsh_get_i32(const char *ns, const char *key, int32_t *out);
esp_err_t nvsh_set_i32(const char *ns, const char *key, int32_t v);
esp_err_t nvsh_erase_key(const char *ns, const char *key);
esp_err_t nvsh_erase_ns(const char *ns);
