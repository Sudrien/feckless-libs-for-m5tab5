/* Test stand-in (u-feckless-0071): feckless_network_handler's routes. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool ethernet_connected(void);
bool ethernet_ip(char *out, size_t out_size);
bool net_online(void);
bool ethernet_link(void);
void net_route_describe(char *out, size_t out_size);
bool net_probe(uint32_t timeout_ms, int *gw_ms, int *net_ms, char *what, size_t what_size);
