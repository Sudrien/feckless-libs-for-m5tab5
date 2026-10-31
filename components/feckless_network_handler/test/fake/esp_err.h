/*
 * Host stand-in for esp_err.h, for these tests only. The player's
 * texttest/fake/esp_err.h forwarded to its shim.h, which is the whole
 * player's ESP-IDF surface and did not come along.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                  -1
#define ESP_ERR_NO_MEM            0x101
#define ESP_ERR_INVALID_ARG       0x102
#define ESP_ERR_INVALID_STATE     0x103
#define ESP_ERR_NOT_FOUND         0x105

#define ESP_LOGE(tag, ...)        do { (void)(tag); } while (0)
#define ESP_LOGW(tag, ...)        do { (void)(tag); } while (0)
#define ESP_LOGI(tag, ...)        do { (void)(tag); } while (0)
