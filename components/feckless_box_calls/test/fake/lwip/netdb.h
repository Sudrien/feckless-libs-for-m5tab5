/* Test stand-in (u-feckless-0071): the host's resolver under lwIP's names. */
#pragma once
#include <netdb.h>
int lwip_getaddrinfo(const char *node, const char *serv, const struct addrinfo *hints, struct addrinfo **res);
void lwip_freeaddrinfo(struct addrinfo *ai);
