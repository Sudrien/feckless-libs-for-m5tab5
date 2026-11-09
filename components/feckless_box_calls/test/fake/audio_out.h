#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "tab5io.h"
typedef enum { AUDIO_CAPTURE_BUILTIN = 0, AUDIO_CAPTURE_HEADSET } audio_capture_src_t;
esp_err_t audio_out_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t exp1, uint32_t rate);
esp_err_t audio_out_set_volume(uint8_t percent);
bool audio_out_headphones(void);
esp_err_t audio_out_capture_begin(audio_capture_src_t src);
size_t audio_out_capture_read(int32_t *frames, size_t max_frames, uint32_t timeout_ms);
void audio_out_capture_end(void);
esp_err_t audio_out_monitor_write(const int16_t *stereo, size_t frames);
