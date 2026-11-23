/* SPDX-License-Identifier: MIT
 *
 * The app's half of the radio's control in a box (u-feckless-0083): its
 * network hooks, asked here, in the app, on the thread that asks for the
 * radio -- never by the host.
 *
 * feckless_net_set_hooks() keeps the app's hooks. wifi_apply_settings(),
 * wifi_request_apply() and wifi_request_restart() ask the three yes/no
 * hooks (wifi_enabled, ntp_enabled, portal_running) first and hand the
 * answers to the host with the request, as feckless_net_set_hooks() does
 * once at the start; the launcher's network reads the answers last given
 * wherever it would have called the hooks (feckless_box_calls' box_wifi.c
 * says why it never calls into the app for them). The notes -- heap_note,
 * online, ntp_reply -- and portal_stop the host queues for the app's
 * callback loop, by address. A NULL hook is feckless_net.h's default:
 * the radio wanted, NTP not, no portal.
 *
 * So an app that changes a setting and asks for it to be applied is read
 * afresh, as the network's README asks; one that changes the NTP setting
 * alone is read at its next request.
 *
 * wifi_auth_name() is here too: a host string the app could not read.
 *
 * u-feckless-0084: and wifi_ap_netif(), a host pointer the app could not
 * hold: the box's own number for the AP's netif, from
 * esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") (u-feckless-0076), while
 * wifi_ap_ip() says the AP is up; NULL otherwise, as the host's.
 *
 * Linked only into an ELF that calls one of these: the linker takes this
 * file for those names.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;

/* feckless_network_handler's feckless_net_hooks_t, member for member:
 * this component does not depend on that one's headers. */
typedef struct {
    void (*heap_note)(const char *why);
    void (*online)(void);
    bool (*ntp_enabled)(void);
    bool (*ntp_reply)(int64_t epoch, int64_t boot_us);
    bool (*wifi_enabled)(void);
    bool (*portal_running)(void);
    esp_err_t (*portal_stop)(void);
    void *usb_register_class;               /* never the app's: the launcher's is kept */
} box_net_hooks_t;

/* the host's (feckless_abi's box section) */
esp_err_t feckless_box_net_hooks(uint32_t heap_note, uint32_t online, uint32_t ntp_reply, uint32_t portal_stop,
                                 uint32_t answers);
esp_err_t feckless_box_wifi_apply(uint32_t answers, uint32_t how, const char *why);
/* the host's (the network section) */
bool wifi_ap_ip(char *out, size_t out_size);
void *esp_netif_get_handle_from_ifkey(const char *if_key);

#define ANS_WIFI   1u
#define ANS_NTP    2u
#define ANS_PORTAL 4u

static box_net_hooks_t s_hooks;

static uint32_t answers(void)
{
    uint32_t a = 0;
    if (!s_hooks.wifi_enabled || s_hooks.wifi_enabled()) a |= ANS_WIFI;
    if (s_hooks.ntp_enabled && s_hooks.ntp_enabled()) a |= ANS_NTP;
    if (s_hooks.portal_running && s_hooks.portal_running()) a |= ANS_PORTAL;
    return a;
}

void feckless_net_set_hooks(const box_net_hooks_t *hooks)
{
    if (hooks) s_hooks = *hooks;
    else s_hooks = (box_net_hooks_t){ 0 };
    (void)feckless_box_net_hooks((uint32_t)(uintptr_t)s_hooks.heap_note, (uint32_t)(uintptr_t)s_hooks.online,
                                 (uint32_t)(uintptr_t)s_hooks.ntp_reply, (uint32_t)(uintptr_t)s_hooks.portal_stop,
                                 answers());
}

esp_err_t wifi_apply_settings(void) { return feckless_box_wifi_apply(answers(), 0, 0); }
void wifi_request_apply(void) { (void)feckless_box_wifi_apply(answers(), 1, 0); }
void wifi_request_restart(const char *why) { (void)feckless_box_wifi_apply(answers(), 2, why); }

/* wifi.c's authmode(), by wifi_auth_mode_t's numbers (ESP-IDF 6.1) */
const char *wifi_auth_name(uint8_t auth)
{
    switch (auth) {
    case 0:  return "open";
    case 1:  return "WEP";
    case 2:  return "WPA";
    case 3:  return "WPA2";
    case 4:  return "WPA/WPA2";
    case 5:  return "WPA2-ENT";
    case 6:  return "WPA3";
    case 7:  return "WPA2/WPA3";
    case 8:  return "WAPI";
    default: return "?";
    }
}

/* wifi.h's struct esp_netif_obj *, opaque here */
void *wifi_ap_netif(void)
{
    char ip[16];
    return wifi_ap_ip(ip, sizeof ip) ? esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") : 0;
}
