#pragma once
/* u-feckless-0065: the cache's line at an address; PSRAM's on the P4 is 128 */
#include <stddef.h>
size_t esp_cache_get_line_size_by_addr(const void *addr);
