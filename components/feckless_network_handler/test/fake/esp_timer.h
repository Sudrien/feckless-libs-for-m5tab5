/* Host stand-in for esp_timer.h, for fetchtest (9003). SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
