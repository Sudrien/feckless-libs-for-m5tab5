/* Test stand-in (u-feckless-0071, 0083) */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
int wifistore_count(void);
esp_err_t wifistore_save(const char *ssid, const char *secret, bool is_psk);
