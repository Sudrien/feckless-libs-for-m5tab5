/* Stand-in for the build's sdkconfig.h: assembler-only checks. The guard
 * path is chosen by -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=1 or =0. */
#pragma once
#ifndef CONFIG_ESP_SYSTEM_HW_STACK_GUARD
#define CONFIG_ESP_SYSTEM_HW_STACK_GUARD 0
#endif
