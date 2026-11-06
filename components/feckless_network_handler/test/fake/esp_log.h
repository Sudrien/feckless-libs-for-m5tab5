/*
 * Host stand-in for esp_log.h, for fetchtest only (9003). Unlike
 * esp_err.h's swallowing macros, these format their arguments, so a
 * variable used only in a log line is used, and the format is checked.
 * FETCHTEST_LOG=1 in the environment prints them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "esp_err.h"

void fake_log(char level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#define ESP_LOGE(tag, ...) fake_log('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) fake_log('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) fake_log('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) fake_log('D', tag, __VA_ARGS__)
