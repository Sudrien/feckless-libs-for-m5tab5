#pragma once
/* u-feckless-0077: feckless_network_handler's TLS server, as box_httpd.c calls it */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
esp_err_t feckless_https_start(httpd_handle_t *out, const httpd_config_t *cfg, uint16_t port);
esp_err_t feckless_https_stop(httpd_handle_t handle);
esp_err_t feckless_https_redirect(httpd_req_t *req);
#define FECKLESS_HTTPS_FP_SIZE 96
bool feckless_https_fingerprint(char *out, size_t out_size);
