/* Host stand-in for esp_log.h: logs are counted, not printed. SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
extern int fake_log_warn, fake_log_err;
#define ESP_LOGE(tag, ...) do { (void)(tag); fake_log_err++; } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); fake_log_warn++; } while (0)
#define ESP_LOGI(tag, ...) do { (void)(tag); } while (0)
