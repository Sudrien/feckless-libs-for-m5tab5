/*
 * Host stand-in for IDF v6.1's esp_http_client.h (u-feckless-0073): the
 * config with its fields in IDF's order, the options that add fields left
 * off as a launcher without them has it, and the calls box_http.c makes.
 * fake_http.c answers them over a socketpair.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define CONFIG_ESP_HTTP_CLIENT_SAVE_RESPONSE_HEADERS 1

typedef struct esp_http_client *esp_http_client_handle_t;
typedef struct esp_http_client_event *esp_http_client_event_handle_t;
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_handle_t evt);
typedef enum { HTTP_AUTH_TYPE_NONE = 0, HTTP_AUTH_TYPE_BASIC, HTTP_AUTH_TYPE_DIGEST } esp_http_client_auth_type_t;
typedef enum { ESP_HTTP_CLIENT_TLS_VER_ANY = 0 } esp_http_client_proto_ver_t;
typedef enum { HTTP_METHOD_GET = 0, HTTP_METHOD_POST, HTTP_METHOD_PUT, HTTP_METHOD_PATCH, HTTP_METHOD_DELETE,
               HTTP_METHOD_HEAD } esp_http_client_method_t;
typedef enum { HTTP_TRANSPORT_UNKNOWN = 0, HTTP_TRANSPORT_OVER_TCP, HTTP_TRANSPORT_OVER_SSL } esp_http_client_transport_t;
typedef enum { HTTP_ADDR_TYPE_UNSPEC = 0 } esp_http_client_addr_type_t;
typedef struct esp_key_config esp_key_config_t;
struct ifreq;

typedef struct {
    const char *url;
    const char *host;
    int port;
    const char *username;
    const char *password;
    esp_http_client_auth_type_t auth_type;
    const char *path;
    const char *query;
    union {
        const char *cert_pem;
        const char *cert_der;
    };
    size_t cert_len;
    union {
        const char *client_cert_pem;
        const char *client_cert_der;
    };
    size_t client_cert_len;
    const char *client_key_pem;
    size_t client_key_len;
    const esp_key_config_t *client_key;
    const char *client_key_password;
    size_t client_key_password_len;
    esp_http_client_proto_ver_t tls_version;
    const char *user_agent;
    esp_http_client_method_t method;
    int timeout_ms;
    bool disable_auto_redirect;
    int max_redirection_count;
    int max_authorization_retries;
    http_event_handle_cb event_handler;
    esp_http_client_transport_t transport_type;
    int buffer_size;
    int buffer_size_tx;
    void *user_data;
    bool is_async;
    bool use_global_ca_store;
    bool skip_cert_common_name_check;
    const char *common_name;
    esp_err_t (*crt_bundle_attach)(void *conf);
    bool keep_alive_enable;
    int keep_alive_idle;
    int keep_alive_interval;
    int keep_alive_count;
    struct ifreq *if_name;
    esp_http_client_addr_type_t addr_type;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char *key, char **value);
esp_err_t esp_http_client_get_response_header(esp_http_client_handle_t client, const char *key, char **value);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout_ms);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t client);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int esp_http_client_write(esp_http_client_handle_t client, const char *buffer, int len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int len);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_get_url(esp_http_client_handle_t client, char *url, const int len);
int esp_http_client_get_socket(esp_http_client_handle_t client);
