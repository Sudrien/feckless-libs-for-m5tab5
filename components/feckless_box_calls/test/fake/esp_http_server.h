/*
 * Host stand-in for ESP-IDF 6.1's esp_http_server.h (u-feckless-0077): the
 * structs as IDF lays them out (WebSockets on, no handshake callbacks), and
 * the calls box_httpd.c makes. fake_httpd.c answers them, and lets a test
 * play the server's task: a connection opening, a request, a frame.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define CONFIG_HTTPD_WS_SUPPORT 1
#define CONFIG_HTTPD_MAX_URI_LEN 512

enum http_method { HTTP_DELETE = 0, HTTP_GET = 1, HTTP_HEAD = 2, HTTP_POST = 3, HTTP_PUT = 4 };
typedef enum http_method httpd_method_t;
#define HTTP_ANY INT_MAX

#define ESP_ERR_HTTPD_BASE          (0xb000)
#define ESP_ERR_HTTPD_HANDLERS_FULL (ESP_ERR_HTTPD_BASE + 1)
#define ESP_ERR_HTTPD_RESULT_TRUNC  (ESP_ERR_HTTPD_BASE + 4)
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_SOCK_ERR_FAIL    -1
#define HTTPD_SOCK_ERR_INVALID -2

typedef void *httpd_handle_t;
typedef void (*httpd_free_ctx_fn_t)(void *ctx);
typedef esp_err_t (*httpd_open_func_t)(httpd_handle_t hd, int sockfd);
typedef void (*httpd_close_func_t)(httpd_handle_t hd, int sockfd);
typedef bool (*httpd_uri_match_func_t)(const char *reference_uri, const char *uri_to_match, size_t match_upto);

typedef struct httpd_config {
    unsigned task_priority;
    size_t stack_size;
    BaseType_t core_id;
    uint32_t task_caps;
    size_t max_req_hdr_len;
    size_t max_uri_len;
    uint16_t server_port;
    uint16_t ctrl_port;
    uint16_t max_open_sockets;
    uint16_t max_uri_handlers;
    uint16_t max_resp_headers;
    uint16_t backlog_conn;
    bool lru_purge_enable;
    uint16_t recv_wait_timeout;
    uint16_t send_wait_timeout;
    void *global_user_ctx;
    httpd_free_ctx_fn_t global_user_ctx_free_fn;
    void *global_transport_ctx;
    httpd_free_ctx_fn_t global_transport_ctx_free_fn;
    bool enable_so_linger;
    int linger_timeout;
    bool keep_alive_enable;
    int keep_alive_idle;
    int keep_alive_interval;
    int keep_alive_count;
    struct ifreq *if_name;
    httpd_open_func_t open_fn;
    httpd_close_func_t close_fn;
    httpd_uri_match_func_t uri_match_fn;
} httpd_config_t;

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char uri[CONFIG_HTTPD_MAX_URI_LEN + 1];
    size_t content_len;
    void *aux;
    void *user_ctx;
    void *sess_ctx;
    httpd_free_ctx_fn_t free_ctx;
    bool ignore_sess_ctx_changes;
} httpd_req_t;

typedef struct httpd_uri {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
    bool is_websocket;
    bool handle_ws_control_frames;
    const char *supported_subprotocol;
} httpd_uri_t;

typedef enum {
    HTTPD_500_INTERNAL_SERVER_ERROR = 0, HTTPD_501_METHOD_NOT_IMPLEMENTED, HTTPD_505_VERSION_NOT_SUPPORTED,
    HTTPD_400_BAD_REQUEST, HTTPD_401_UNAUTHORIZED, HTTPD_403_FORBIDDEN, HTTPD_404_NOT_FOUND,
} httpd_err_code_t;

typedef enum {
    HTTPD_WS_TYPE_CONTINUE = 0x0, HTTPD_WS_TYPE_TEXT = 0x1, HTTPD_WS_TYPE_BINARY = 0x2,
    HTTPD_WS_TYPE_CLOSE = 0x8, HTTPD_WS_TYPE_PING = 0x9, HTTPD_WS_TYPE_PONG = 0xA,
} httpd_ws_type_t;
typedef enum { HTTPD_WS_CLIENT_INVALID = 0, HTTPD_WS_CLIENT_HTTP = 1, HTTPD_WS_CLIENT_WEBSOCKET = 2 } httpd_ws_client_info_t;
typedef struct httpd_ws_frame {
    bool final;
    bool fragmented;
    httpd_ws_type_t type;
    uint8_t *payload;
    size_t len;
    size_t left_len;
} httpd_ws_frame_t;
typedef void (*httpd_work_fn_t)(void *arg);

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle, const char *uri, httpd_method_t method);
int httpd_req_to_sockfd(httpd_req_t *r);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *field);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size);
size_t httpd_req_get_url_query_len(httpd_req_t *r);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t val_size);
esp_err_t httpd_req_get_cookie_val(httpd_req_t *req, const char *cookie_name, char *val, size_t *val_size);
bool httpd_uri_match_wildcard(const char *uri_template, const char *uri_to_match, size_t match_upto);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *msg);
void *httpd_get_global_user_ctx(httpd_handle_t handle);
esp_err_t httpd_sess_trigger_close(httpd_handle_t handle, int sockfd);
esp_err_t httpd_get_client_list(httpd_handle_t handle, size_t *fds, int *client_fds);
esp_err_t httpd_queue_work(httpd_handle_t handle, httpd_work_fn_t work, void *arg);
esp_err_t httpd_ws_recv_frame(httpd_req_t *req, httpd_ws_frame_t *pkt, size_t max_len);
esp_err_t httpd_ws_send_frame(httpd_req_t *req, httpd_ws_frame_t *pkt);
esp_err_t httpd_ws_send_frame_async(httpd_handle_t hd, int fd, httpd_ws_frame_t *frame);
httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t hd, int fd);
