/* Test stand-in (u-feckless-0071) */
#pragma once
#include <stdbool.h>
#include <stddef.h>
bool wifi_up(void);
bool wifi_connected(void);
bool wifi_ntp_synced(void);
bool wifi_sta_ssid(char *out, size_t out_size);
bool wifi_sta_ip(char *out, size_t out_size);
