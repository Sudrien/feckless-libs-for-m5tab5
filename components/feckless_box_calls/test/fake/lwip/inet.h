/* Test stand-in (u-feckless-0071) */
#pragma once
#include <stdint.h>
#ifndef IPADDR_NONE
#define IPADDR_NONE ((uint32_t)0xffffffffUL)
#endif
uint32_t ipaddr_addr(const char *cp);
