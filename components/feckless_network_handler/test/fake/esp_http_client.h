/*
 * Host stand-in for esp_http_client.h, for fetchtest (9003): the part of
 * IDF's API feckless_fetch.c uses, with its types' names and fields, over
 * an in-memory server that fetchfake.h sets up.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct esp_http_client *esp_http_client_handle_t;

typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADERS_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED,
} esp_http_client_event_id_t;

typedef struct {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t   client;
    void                      *data;
    int                        data_len;
    void                      *user_data;
    char                      *header_key;
    char                      *header_value;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);

typedef struct {
    const char          *url;
    int                  timeout_ms;
    int                  buffer_size;
    int                  buffer_size_tx;
    const char          *user_agent;
    bool                 disable_auto_redirect;
    http_event_handle_cb event_handler;
    void                *user_data;
    esp_err_t          (*crt_bundle_attach)(void *conf);
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len);
int64_t   esp_http_client_fetch_headers(esp_http_client_handle_t c);
int       esp_http_client_get_status_code(esp_http_client_handle_t c);
int64_t   esp_http_client_get_content_length(esp_http_client_handle_t c);
int       esp_http_client_read(esp_http_client_handle_t c, char *buf, int len);
bool      esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
