/*
 * Host stand-in for nvs.h: an in-memory store with namespaces, typed
 * values and IDF's error codes, enough to run nvs_handler.c as it is.
 * Only what nvs_handler.c calls. SPDX-License-Identifier: MIT
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ESP_ERR_NVS_NOT_FOUND        0x1102
#define ESP_ERR_NVS_TYPE_MISMATCH    0x1103
#define ESP_ERR_NVS_INVALID_LENGTH   0x110c
#define ESP_ERR_NVS_NO_FREE_PAGES    0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x110e
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE 0x1105

typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef int nvs_handle_t;

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);
void      nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t len);
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len);
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *out);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *out);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *out);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v);

/* The fake's own controls. */
void fake_nvs_reset(void);
int  fake_nvs_fail_writes_after(int n);   /* -1: never; else the n-th write fails */
int  fake_nvs_has_ns(const char *ns);     /* does the namespace hold any key? */
int  fake_nvs_writes(void);
