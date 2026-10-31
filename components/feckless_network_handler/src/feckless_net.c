/*
 * feckless_net.c -- the application's hooks, and feckless_net_start(). See
 * feckless_net.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "feckless_net.h"

#include "esp_log.h"
#include "ethcfg.h"
#include "ethernet.h"
#include "wifi.h"
#include "wifistore.h"

static const char *TAG = "feckless_net";

/* 6005: two buffers and a pointer, so the hooks can be set after the
 * network is up without a reader seeing half of a struct. */
static feckless_net_hooks_t s_hooks[2];
static feckless_net_hooks_t *volatile s_cur = &s_hooks[0];

void feckless_net_set_hooks(const feckless_net_hooks_t *hooks)
{
    feckless_net_hooks_t *next = (s_cur == &s_hooks[0]) ? &s_hooks[1] : &s_hooks[0];
    esp_err_t (*keep)(const char *, feckless_net_usb_class_fn) = s_cur->usb_register_class;
    if (hooks) *next = *hooks;
    else       *next = (feckless_net_hooks_t){ 0 };
    if (!next->usb_register_class) next->usb_register_class = keep;
    __atomic_store_n(&s_cur, next, __ATOMIC_RELEASE);
}

const feckless_net_hooks_t *feckless_net_hooks(void)
{
    return __atomic_load_n(&s_cur, __ATOMIC_ACQUIRE);
}

static bool s_started;

bool feckless_net_started(void) { return s_started; }

esp_err_t feckless_net_start(const feckless_net_start_t *cfg)
{
    if (s_started) return ESP_OK;
    if (!cfg || !cfg->exp2 || !cfg->usb_register_class) return ESP_ERR_INVALID_ARG;
    wifistore_init(cfg->nvs_ns);
    if (cfg->usb_set_config_select) cfg->usb_set_config_select(ethcfg_select);
    feckless_net_hooks_t h = *feckless_net_hooks();
    h.usb_register_class = cfg->usb_register_class;
    feckless_net_set_hooks(&h);
    wifi_init((i2c_master_dev_handle_t)cfg->exp2);
    const esp_err_t eth = ethernet_init();
    if (eth != ESP_OK) ESP_LOGW(TAG, "no USB Ethernet: %s", esp_err_to_name(eth));
    s_started = true;
    ESP_LOGI(TAG, "network up to be asked for: %d saved network%s, Ethernet %s",
             wifistore_count(), wifistore_count() == 1 ? "" : "s",
             eth == ESP_OK ? "registered" : "not registered");
    return ESP_OK;
}
