/* SPDX-License-Identifier: MIT
 *
 * esp_netif's handles for a boxed app (u-feckless-0076): numbers for the
 * interfaces, looked up again at every use.
 *
 * - An interface is a number in the app's hands, TOKEN_NETIF + slot, the
 *   same number for the same interface while it lasts. A USB Ethernet
 *   dongle pulled out takes its netif with it; a number for it then
 *   answers ESP_ERR_INVALID_ARG (0 addresses), never a freed pointer --
 *   each call finds the interface again inside esp_netif_find_if(), which
 *   holds the list, and reads it there.
 * - esp_netif_find_if() is the app's own (feckless_box_libc's
 *   src/box_netif.c): its predicate is the app's code, called in the app,
 *   for each number feckless_box_netif_next() gives. A box does not call
 *   into its app; this way nothing has to.
 * - Answered: esp_netif_get_ip_info, esp_netif_get_all_ip6,
 *   esp_netif_get_handle_from_ifkey. The DHCP server and the DNS setters
 *   change an interface the launcher owns, and stay unanswered.
 *
 * In the network section; built where box_net.c is.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("ethernet.h") && __has_include("lwip/sockets.h") && __has_include("esp_netif.h") && \
    !defined(FECKLESS_BOX_NO_NET)

#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "lwip/sockets.h"                   /* lwIP's options: LWIP_IPV6_NUM_ADDRESSES */

#define TOKEN_NETIF 0x4e494600u             /* "NIF\0" + slot */
#define NETIFS      8
#define KEY_MAX     32

static esp_netif_t *s_nif[NETIFS];         /* a slot's interface, as last seen */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---- the list, in esp_netif_find_if()'s hold ---------------------------- */

typedef struct {
    esp_netif_t *seen[NETIFS];
    int n;
} list_t;

static bool collect(esp_netif_t *nif, void *ctx)
{
    list_t *l = ctx;
    if (l->n < NETIFS) l->seen[l->n++] = nif;
    return false;                           /* all of them */
}

/* The slot for an interface, made if it has none: a slot never used, else
 * one whose interface is no longer in `live` (so a number pulled out keeps
 * meaning nothing for as long as there is room). -1 if all are taken by
 * interfaces still live. */
static int slot_for(esp_netif_t *nif, const list_t *live)
{
    portENTER_CRITICAL(&s_mux);
    int got = -1, empty = -1, dead = -1;
    for (int i = 0; i < NETIFS && got < 0; i++) {
        if (s_nif[i] == nif) got = i;
        else if (!s_nif[i]) { if (empty < 0) empty = i; }
        else if (dead < 0) {
            bool alive = false;
            for (int k = 0; k < live->n; k++) alive |= live->seen[k] == s_nif[i];
            if (!alive) dead = i;
        }
    }
    const int spare = empty >= 0 ? empty : dead;
    if (got < 0 && spare >= 0) {
        s_nif[spare] = nif;
        got = spare;
    }
    portEXIT_CRITICAL(&s_mux);
    return got;
}

static esp_netif_t *slot_nif(uint32_t tok)
{
    const uint32_t i = tok - TOKEN_NETIF;
    if (i >= NETIFS) return NULL;
    portENTER_CRITICAL(&s_mux);
    esp_netif_t *n = s_nif[i];
    portEXIT_CRITICAL(&s_mux);
    return n;
}

/* (previous number, 0 for the first) -> the next interface's number, 0
 * after the last. The list as it is now. */
H(c_feckless_box_netif_next)
{
    (void)w;
    *ret = 0;
    list_t l = { 0 };
    (void)esp_netif_find_if(collect, &l);
    esp_netif_t *prev = a[0] ? slot_nif(a[0]) : NULL;
    int from = 0;
    if (a[0]) {
        from = l.n;                         /* gone: nothing after it */
        for (int k = 0; prev && k < l.n; k++) {
            if (l.seen[k] == prev) { from = k + 1; break; }
        }
    }
    for (int k = from; k < l.n; k++) {
        const int s = slot_for(l.seen[k], &l);
        if (s >= 0) { *ret = TOKEN_NETIF + (uint32_t)s; break; }
    }
    return true;
}

/* ---- one interface, read where the list is held ------------------------- */

typedef struct {
    esp_netif_t *want;
    bool found;
    esp_netif_ip_info_t ip;
    esp_err_t e;
#if CONFIG_LWIP_IPV6
    esp_ip6_addr_t ip6[LWIP_IPV6_NUM_ADDRESSES];
#endif
    int n6;
    bool six;
} read_t;

static bool read_one(esp_netif_t *nif, void *ctx)
{
    read_t *r = ctx;
    if (nif != r->want) return false;
    r->found = true;
    if (r->six) {
#if CONFIG_LWIP_IPV6
        r->n6 = esp_netif_get_all_ip6(nif, r->ip6);
#endif
    } else {
        r->e = esp_netif_get_ip_info(nif, &r->ip);
    }
    return true;
}

/* (number, esp_netif_ip_info_t *) */
H(c_esp_netif_get_ip_info)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    if (!ok_ptr(w, a[1], sizeof(esp_netif_ip_info_t))) return true;
    read_t r = { .want = slot_nif(a[0]) };
    if (!r.want) return true;
    (void)esp_netif_find_if(read_one, &r);
    if (!r.found) return true;              /* pulled out since */
    if (r.e == ESP_OK) memcpy((void *)(uintptr_t)a[1], &r.ip, sizeof r.ip);
    *ret = (uint32_t)r.e;
    return true;
}

/* (number, esp_ip6_addr_t [LWIP_IPV6_NUM_ADDRESSES]) -> how many */
H(c_esp_netif_get_all_ip6)
{
    *ret = 0;
#if CONFIG_LWIP_IPV6
    if (!ok_ptr(w, a[1], sizeof(esp_ip6_addr_t) * LWIP_IPV6_NUM_ADDRESSES)) return true;
    read_t r = { .want = slot_nif(a[0]), .six = true };
    if (!r.want) return true;
    (void)esp_netif_find_if(read_one, &r);
    if (!r.found || r.n6 <= 0) return true;
    const int n = r.n6 > LWIP_IPV6_NUM_ADDRESSES ? LWIP_IPV6_NUM_ADDRESSES : r.n6;
    memcpy((void *)(uintptr_t)a[1], r.ip6, sizeof(esp_ip6_addr_t) * (size_t)n);
    *ret = (uint32_t)n;
#else
    (void)w; (void)a;
#endif
    return true;
}

typedef struct {
    const char *key;
    esp_netif_t *got;
} ifkey_t;

static bool by_key(esp_netif_t *nif, void *ctx)
{
    ifkey_t *k = ctx;
    const char *mine = esp_netif_get_ifkey(nif);
    if (!mine || strcmp(mine, k->key) != 0) return false;
    k->got = nif;
    return true;
}

/* (key) -> its number, or 0 */
H(c_esp_netif_get_handle_from_ifkey)
{
    *ret = 0;
    const char *s = str(w, a[0], KEY_MAX);
    if (!s) return true;
    char key[KEY_MAX];
    const size_t kl = strnlen(s, sizeof key - 1);
    memcpy(key, s, kl);
    key[kl] = '\0';
    ifkey_t k = { .key = key };
    (void)esp_netif_find_if(by_key, &k);
    if (!k.got) return true;
    list_t l = { 0 };
    (void)esp_netif_find_if(collect, &l);
    const int s2 = slot_for(k.got, &l);
    if (s2 >= 0) *ret = TOKEN_NETIF + (uint32_t)s2;
    return true;
}

const feckless_umode_call_t box_netif_calls[] = {
    FN(feckless_box_netif_next), FN(esp_netif_get_ip_info), FN(esp_netif_get_all_ip6),
    FN(esp_netif_get_handle_from_ifkey),
};
const uint32_t box_netif_calls_n = sizeof box_netif_calls / sizeof box_netif_calls[0];

void box_netif_end(void)
{
    portENTER_CRITICAL(&s_mux);
    memset(s_nif, 0, sizeof s_nif);
    portEXIT_CRITICAL(&s_mux);
}

#else

const feckless_umode_call_t box_netif_calls[1];
const uint32_t box_netif_calls_n = 0;
void box_netif_end(void) {}

#endif
