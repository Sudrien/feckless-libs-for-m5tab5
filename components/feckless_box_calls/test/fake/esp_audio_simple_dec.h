#pragma once
/* u-feckless-0052: esp_audio_codec 2.5's simple decoder, as box_codec.c uses it */
#include "esp_audio_types.h"
typedef enum {
    ESP_AUDIO_SIMPLE_DEC_TYPE_NONE = 0, ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC = 3, ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC = 0x414c4143,
    ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS = 0x564f5242,
} esp_audio_simple_dec_type_t;
typedef enum { ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE = 0 } esp_audio_simple_dec_recovery_t;
typedef void *esp_audio_simple_dec_handle_t;
typedef struct { esp_audio_simple_dec_type_t dec_type; void *dec_cfg; int cfg_size; bool use_frame_dec; } esp_audio_simple_dec_cfg_t;
typedef struct { uint8_t *buffer; uint32_t len; bool eos; uint32_t consumed; esp_audio_simple_dec_recovery_t frame_recover; } esp_audio_simple_dec_raw_t;
typedef struct { uint8_t *buffer; uint32_t len; uint32_t needed_size; uint32_t decoded_size; } esp_audio_simple_dec_out_t;
typedef struct { uint32_t sample_rate; uint8_t bits_per_sample; uint8_t channel; uint32_t bitrate; uint32_t frame_size; } esp_audio_simple_dec_info_t;
esp_audio_err_t esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *cfg, esp_audio_simple_dec_handle_t *h);
esp_audio_err_t esp_audio_simple_dec_process(esp_audio_simple_dec_handle_t h, esp_audio_simple_dec_raw_t *raw,
                                             esp_audio_simple_dec_out_t *out);
esp_audio_err_t esp_audio_simple_dec_get_info(esp_audio_simple_dec_handle_t h, esp_audio_simple_dec_info_t *info);
void esp_audio_simple_dec_close(esp_audio_simple_dec_handle_t h);
