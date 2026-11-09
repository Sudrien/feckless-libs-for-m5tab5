#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t uac_init(void);
bool uac_present(void);
esp_err_t uac_stream_start(uint32_t rate, uint8_t channels);
void uac_stream_stop(void);
esp_err_t uac_write(const void *data, size_t len, uint32_t timeout_ms);
const char *uac_product(void);
bool uac_mic_announced(void);
esp_err_t uac_mic_open_bits(uint32_t *rate, uint8_t *channels, uint8_t max_bits, uint8_t *bits);
size_t uac_mic_read(int32_t *frames, size_t max_frames, uint32_t timeout_ms);
bool uac_mic_gone(void);
void uac_mic_close(void);
const char *uac_mic_product(void);
