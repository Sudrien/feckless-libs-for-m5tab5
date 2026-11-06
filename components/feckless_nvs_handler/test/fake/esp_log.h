/* Host stand-in for esp_log.h: logs are counted, not printed. SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
extern int fake_log_warn, fake_log_err;
#define ESP_LOGE(tag, ...) do { (void)(tag); fake_log_err++; } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); fake_log_warn++; } while (0)
/* 9005: info lines are formatted and kept, for nvsh_log()'s test. */
void fake_log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern char fake_log_text[];    /* every info line since the last reset, newline-separated */
#define ESP_LOGI(tag, ...) do { (void)(tag); fake_log_info(__VA_ARGS__); } while (0)
