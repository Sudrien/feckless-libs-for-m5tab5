/* Host stand-in for sdkconfig.h, for fetchtest (9003): helper stacks
 * allowed in PSRAM, as Defeatist's build has it. SPDX-License-Identifier: MIT */
#pragma once
#define CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM 1
