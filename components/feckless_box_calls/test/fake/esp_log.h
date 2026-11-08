#pragma once
#include <stdint.h>
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
typedef struct { union { struct { uint32_t log_level : 3, rest : 29; } opts; uint32_t data; }; } esp_log_config_t;
#define ESP_LOG_CONFIG_LEVEL_MASK 7
void esp_log(esp_log_config_t config, const char *tag, const char *format, ...);
void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...);
uint32_t esp_log_timestamp(void);
