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
/* u-feckless-0046 */
typedef enum { AUDIO_OUT_SPEAKER = 0, AUDIO_OUT_HEADPHONES, AUDIO_OUT_USB } audio_out_route_t;
esp_err_t audio_out_write(const void *data, size_t len);
esp_err_t audio_out_set_format(uint32_t rate, uint8_t channels);
bool audio_out_rate_supported(uint32_t rate);
uint32_t audio_out_rate(void);
void audio_out_set_mute(bool muted);
bool audio_out_muted(void);
void audio_out_set_idle(bool idle);
audio_out_route_t audio_out_route(void);
const char *audio_out_route_name(void);
