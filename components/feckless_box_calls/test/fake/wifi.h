/* Test stand-in (u-feckless-0071, 0083) */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
bool wifi_up(void);
bool wifi_connected(void);
bool wifi_ntp_synced(void);
bool wifi_sta_ssid(char *out, size_t out_size);
bool wifi_sta_ip(char *out, size_t out_size);
/* u-feckless-0083: the radio's control */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;
    uint8_t channel;
    bool    hidden;
    uint8_t bssid[6];
} wifi_seen_t;
int wifi_scan_list(wifi_seen_t *out, int max);
esp_err_t wifi_join(const char *ssid, const char *secret, uint32_t timeout_ms);
esp_err_t wifi_psk(const char *ssid, const char *passphrase, uint8_t key[32]);
esp_err_t wifi_apply_settings(void);
void wifi_request_apply(void);
void wifi_request_restart(const char *why);
/* u-feckless-0084: the access point */
esp_err_t wifi_ap_begin(const char *ssid);
esp_err_t wifi_ap_begin_secure(const char *ssid, const char *pass);
esp_err_t wifi_ap_end(void);
int wifi_ap_clients(void);
bool wifi_ap_ip(char *out, size_t out_size);
