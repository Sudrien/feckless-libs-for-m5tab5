/* SPDX-License-Identifier: MIT
 * u-feckless-0077: the test's side of fake_httpd.c -- the server's task. */
#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

typedef struct {
    esp_err_t ret;                          /* the handler's */
    bool no_handler, sent, done;
    int err;                                /* httpd_resp_send_err's code + 1 */
    int chunks, ws_sent, blocked_recv_got;
    char status[64], type[64], hdrs[512], body[1024];
} fake_resp_t;

int fake_httpd_open(httpd_handle_t h);
void fake_httpd_close(httpd_handle_t h, int fd);
int fake_httpd_peer(httpd_handle_t h, int fd);
esp_err_t fake_httpd_request(httpd_handle_t h, int fd, int method, const char *uri, const char *hdrs,
                             const char *body, fake_resp_t *out);
int fake_httpd_run_work(httpd_handle_t h);
bool fake_httpd_is_tls(httpd_handle_t h);
const httpd_config_t *fake_httpd_cfg(httpd_handle_t h);
int fake_httpd_closes_asked(httpd_handle_t h);
