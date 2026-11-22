/*
 * Host stand-in for esp_netif.h (u-feckless-0076): the types and the calls
 * box_netif.c makes. fake_net.c keeps a list of interfaces a test can
 * change, as a USB dongle coming and going does.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CONFIG_LWIP_IPV6 1
#define LWIP_IPV6_NUM_ADDRESSES 3

typedef struct esp_netif_obj esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint32_t addr[4]; uint8_t zone; } esp_ip6_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef bool (*esp_netif_find_predicate_t)(esp_netif_t *netif, void *ctx);

esp_netif_t *esp_netif_find_if(esp_netif_find_predicate_t fn, void *ctx);
esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info);
int esp_netif_get_all_ip6(esp_netif_t *esp_netif, esp_ip6_addr_t if_ip6[]);
const char *esp_netif_get_ifkey(esp_netif_t *esp_netif);
