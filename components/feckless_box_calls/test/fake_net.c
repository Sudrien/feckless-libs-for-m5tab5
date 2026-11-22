/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0071: lwIP's socket names over the host's own sockets, so
 * box_net.c runs against real loopback connections; feckless_network_
 * handler's routes as values a test sets; and the app's lock, counted.
 */
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "feckless_umode_load.h"

int lwip_socket(int d, int t, int p) { return socket(d, t, p); }
int lwip_bind(int s, const struct sockaddr *n, socklen_t l) { return bind(s, n, l); }
int lwip_connect(int s, const struct sockaddr *n, socklen_t l) { return connect(s, n, l); }
int lwip_listen(int s, int b) { return listen(s, b); }
int lwip_accept(int s, struct sockaddr *a, socklen_t *l) { return accept(s, a, l); }
ssize_t lwip_send(int s, const void *d, size_t n, int f) { return send(s, d, n, f | MSG_NOSIGNAL); }
ssize_t lwip_sendto(int s, const void *d, size_t n, int f, const struct sockaddr *to, socklen_t tl)
{
    return sendto(s, d, n, f | MSG_NOSIGNAL, to, tl);
}
ssize_t lwip_recv(int s, void *m, size_t n, int f) { return recv(s, m, n, f); }
ssize_t lwip_recvfrom(int s, void *m, size_t n, int f, struct sockaddr *fr, socklen_t *fl)
{
    return recvfrom(s, m, n, f, fr, fl);
}
int lwip_setsockopt(int s, int lv, int o, const void *v, socklen_t l) { return setsockopt(s, lv, o, v, l); }
int lwip_getsockopt(int s, int lv, int o, void *v, socklen_t *l) { return getsockopt(s, lv, o, v, l); }
int lwip_getsockname(int s, struct sockaddr *n, socklen_t *l) { return getsockname(s, n, l); }
int lwip_getpeername(int s, struct sockaddr *n, socklen_t *l) { return getpeername(s, n, l); }
int lwip_shutdown(int s, int h) { return shutdown(s, h); }
int lwip_close(int s) { return close(s); }
int lwip_fcntl(int s, int c, int v) { return fcntl(s, c, v); }
int lwip_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv) { return select(n, r, w, e, tv); }
const char *lwip_inet_ntop(int af, const void *s, char *d, socklen_t n) { return inet_ntop(af, s, d, n); }
int lwip_inet_pton(int af, const char *s, void *d) { return inet_pton(af, s, d); }
int lwip_getaddrinfo(const char *n, const char *s, const struct addrinfo *h, struct addrinfo **r)
{
    return getaddrinfo(n, s, h, r);
}
void lwip_freeaddrinfo(struct addrinfo *ai) { freeaddrinfo(ai); }
uint32_t ipaddr_addr(const char *cp) { return inet_addr(cp); }

/* the routes */
bool fake_net_online, fake_eth_connected, fake_eth_link, fake_wifi_up, fake_wifi_connected, fake_ntp;
int fake_wifistore_count;
char fake_eth_ip[64], fake_sta_ip[64], fake_ssid[64], fake_route[128];
uint32_t fake_probe_timeout;

static bool text(char *out, size_t size, const char *v)
{
    if (!size) return false;
    snprintf(out, size, "%s", v);
    return v[0] != '\0';
}

bool net_online(void) { return fake_net_online; }
bool ethernet_connected(void) { return fake_eth_connected; }
bool ethernet_link(void) { return fake_eth_link; }
bool ethernet_ip(char *out, size_t n) { return text(out, n, fake_eth_ip); }
bool wifi_up(void) { return fake_wifi_up; }
bool wifi_connected(void) { return fake_wifi_connected; }
bool wifi_ntp_synced(void) { return fake_ntp; }
bool wifi_sta_ip(char *out, size_t n) { return text(out, n, fake_sta_ip); }
bool wifi_sta_ssid(char *out, size_t n) { return text(out, n, fake_ssid); }
int wifistore_count(void) { return fake_wifistore_count; }
void net_route_describe(char *out, size_t n) { (void)text(out, n, fake_route); }
bool net_probe(uint32_t t, int *gw, int *net, char *what, size_t n)
{
    fake_probe_timeout = t;
    *gw = 3;
    *net = 41;
    snprintf(what, n, "gateway 3 ms, internet 41 ms, and a long tail of words past the room");
    return true;
}

/* the app's lock: depth now, and how many times taken (leaselock_test has
 * a real one: FAKE_REAL_LOCKS) */
int fake_lock_depth, fake_lock_taken;
#ifndef FAKE_REAL_LOCKS
void feckless_umode_app_lock(feckless_umode_app_t *a) { (void)a; fake_lock_depth++; fake_lock_taken++; }
void feckless_umode_app_unlock(feckless_umode_app_t *a) { (void)a; fake_lock_depth--; }
#endif

/* ---- u-feckless-0076: esp_netif's list ---------------------------------- */

#include "esp_netif.h"

struct esp_netif_obj {
    const char *key;
    uint32_t ip, mask;
    int n6;
    bool live;
};
struct esp_netif_obj fake_nif[3] = {
    { "ETH_DEF", 0x7d01a8c0u, 0x00ffffffu, 2, true },        /* 192.168.1.125/24 */
    { "WIFI_STA_DEF", 0x0a00000au, 0x0000ffffu, 0, true },   /* 10.0.0.10/16 */
    { "ecm", 0, 0, 0, false },
};
int fake_find_if_calls;

esp_netif_t *esp_netif_find_if(esp_netif_find_predicate_t fn, void *ctx)
{
    fake_find_if_calls++;
    for (int i = 0; i < 3; i++)
        if (fake_nif[i].live && fn(&fake_nif[i], ctx)) return &fake_nif[i];
    return NULL;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *ip)
{
    ip->ip.addr = n->ip;
    ip->netmask.addr = n->mask;
    ip->gw.addr = n->ip ? (n->ip & n->mask) | 0xfe000000u : 0;
    return ESP_OK;
}
int esp_netif_get_all_ip6(esp_netif_t *n, esp_ip6_addr_t a[])
{
    for (int i = 0; i < n->n6; i++) { memset(&a[i], 0, sizeof a[i]); a[i].addr[0] = 0xfe80u + (uint32_t)i; }
    return n->n6;
}
const char *esp_netif_get_ifkey(esp_netif_t *n) { return n->key; }
