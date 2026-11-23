/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0083: feckless_network_handler's radio control and hooks,
 * recording what box_wifi.c asks of them.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"
#include "feckless_net.h"
#include "wifi.h"
#include "wifistore.h"

/* the hooks: kept as feckless_net.c keeps them, usb_register_class too */
static feckless_net_hooks_t s_hooks;
int fake_set_hooks_calls;
void feckless_net_set_hooks(const feckless_net_hooks_t *h)
{
    esp_err_t (*keep)(const char *, feckless_net_usb_class_fn) = s_hooks.usb_register_class;
    s_hooks = h ? *h : (feckless_net_hooks_t){ 0 };
    if (!s_hooks.usb_register_class) s_hooks.usb_register_class = keep;
    fake_set_hooks_calls++;
}
const feckless_net_hooks_t *feckless_net_hooks(void) { return &s_hooks; }

/* the switch */
int fake_apply_calls, fake_request_apply_calls, fake_restart_calls;
bool fake_apply_saw_wifi;                   /* what wifi_enabled() said inside the apply */
char fake_restart_why[64];
esp_err_t wifi_apply_settings(void)
{
    fake_apply_calls++;
    fake_apply_saw_wifi = s_hooks.wifi_enabled ? s_hooks.wifi_enabled() : true;
    return ESP_OK;
}
void wifi_request_apply(void) { fake_request_apply_calls++; }
void wifi_request_restart(const char *why)
{
    fake_restart_calls++;
    strncpy(fake_restart_why, why ? why : "(null)", sizeof fake_restart_why - 1);
}

/* the station */
int fake_scan_found = 3, fake_scan_max_asked;
int wifi_scan_list(wifi_seen_t *out, int max)
{
    fake_scan_max_asked = max;
    const int n = fake_scan_found < max ? fake_scan_found : max;
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof out[i]);
        out[i].ssid[0] = (char)('A' + i);
        out[i].rssi = (int8_t)(-40 - i);
        out[i].auth = 3;
        out[i].channel = (uint8_t)(1 + i);
    }
    return n;
}
char fake_join_ssid[40], fake_join_secret[80];
bool fake_join_secret_null;
uint32_t fake_join_timeout;
esp_err_t wifi_join(const char *ssid, const char *secret, uint32_t timeout_ms)
{
    strncpy(fake_join_ssid, ssid, sizeof fake_join_ssid - 1);
    fake_join_secret_null = !secret;
    strncpy(fake_join_secret, secret ? secret : "", sizeof fake_join_secret - 1);
    fake_join_timeout = timeout_ms;
    return ESP_OK;
}
esp_err_t wifi_psk(const char *ssid, const char *pass, uint8_t key[32])
{
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(ssid[0] + pass[0] + i);
    return ESP_OK;
}
char fake_saved_ssid[40], fake_saved_secret[80];
bool fake_saved_psk;
esp_err_t wifistore_save(const char *ssid, const char *secret, bool is_psk)
{
    strncpy(fake_saved_ssid, ssid, sizeof fake_saved_ssid - 1);
    strncpy(fake_saved_secret, secret, sizeof fake_saved_secret - 1);
    fake_saved_psk = is_psk;
    return ESP_OK;
}
esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(0x10 * (int)ifx + i);
    return ESP_OK;
}

/* u-feckless-0084: the access point */
char fake_ap_ssid[40], fake_ap_pass[80];
bool fake_ap_on, fake_ap_secure;
esp_err_t wifi_ap_begin_secure(const char *ssid, const char *pass)
{
    if (pass && (strlen(pass) < 8 || strlen(pass) > 63)) return ESP_ERR_INVALID_ARG;
    strncpy(fake_ap_ssid, ssid, sizeof fake_ap_ssid - 1);
    strncpy(fake_ap_pass, pass ? pass : "", sizeof fake_ap_pass - 1);
    fake_ap_secure = pass != NULL;
    fake_ap_on = true;
    return ESP_OK;
}
esp_err_t wifi_ap_begin(const char *ssid) { return wifi_ap_begin_secure(ssid, NULL); }
esp_err_t wifi_ap_end(void) { fake_ap_on = false; return ESP_OK; }
int wifi_ap_clients(void) { return fake_ap_on ? 2 : 0; }
bool wifi_ap_ip(char *out, size_t n)
{
    if (!fake_ap_on) { if (n) out[0] = 0; return false; }
    snprintf(out, n, "192.168.4.1");
    return true;
}
