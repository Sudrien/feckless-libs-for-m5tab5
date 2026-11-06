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

/* 9005: listing. IDF's type codes, entry info, iterator and stats. */
#define NVS_DEFAULT_PART_NAME "nvs"
typedef enum {
    NVS_TYPE_U8 = 0x01, NVS_TYPE_I8 = 0x11, NVS_TYPE_U16 = 0x02, NVS_TYPE_I16 = 0x12,
    NVS_TYPE_U32 = 0x04, NVS_TYPE_I32 = 0x14, NVS_TYPE_U64 = 0x08, NVS_TYPE_I64 = 0x18,
    NVS_TYPE_STR = 0x21, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff
} nvs_type_t;
typedef struct { char namespace_name[16]; char key[16]; nvs_type_t type; } nvs_entry_info_t;
typedef struct fake_it *nvs_iterator_t;
typedef struct {
    size_t used_entries, free_entries, available_entries, total_entries, namespace_count;
} nvs_stats_t;
esp_err_t nvs_entry_find(const char *part, const char *ns, nvs_type_t type, nvs_iterator_t *it);
esp_err_t nvs_entry_next(nvs_iterator_t *it);
esp_err_t nvs_entry_info(const nvs_iterator_t it, nvs_entry_info_t *out);
void      nvs_release_iterator(nvs_iterator_t it);
esp_err_t nvs_get_stats(const char *part, nvs_stats_t *st);
esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *out);
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *out);
esp_err_t nvs_get_i16(nvs_handle_t h, const char *k, int16_t *out);
esp_err_t nvs_get_u64(nvs_handle_t h, const char *k, uint64_t *out);
esp_err_t nvs_get_i64(nvs_handle_t h, const char *k, int64_t *out);
int       fake_nvs_live_iterators(void);  /* found and not yet released */

/* The fake's own controls. */
void fake_nvs_reset(void);
int  fake_nvs_fail_writes_after(int n);   /* -1: never; else the n-th write fails */
int  fake_nvs_has_ns(const char *ns);     /* does the namespace hold any key? */
int  fake_nvs_writes(void);
