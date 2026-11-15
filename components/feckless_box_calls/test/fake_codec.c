/* SPDX-License-Identifier: MIT
 * u-feckless-0052: a stand-in for esp_audio_codec's simple decoder. It
 * records the configuration and cookie it was opened with, and "decodes"
 * by consuming half the input and writing each input byte doubled. */
#include <stdlib.h>
#include <string.h>
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_alac_dec.h"

int c_live, c_regs, c_simple_regs;
esp_audio_err_t c_reg_result = ESP_AUDIO_ERR_ALREADY_EXIST;
uint8_t c_cookie[64];
uint32_t c_cookie_len;
const void *c_cookie_ptr, *c_raw_ptr, *c_out_ptr;
int c_cfg_size;

typedef struct { int type; } fdec_t;
esp_audio_err_t esp_audio_dec_register_default(void) { c_regs++; return c_reg_result; }
esp_audio_err_t esp_audio_simple_dec_register_default(void) { c_simple_regs++; return ESP_AUDIO_ERR_OK; }
esp_audio_err_t esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *cfg, esp_audio_simple_dec_handle_t *h)
{
    if (cfg->dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) return ESP_AUDIO_ERR_NOT_SUPPORT;
    c_cfg_size = cfg->cfg_size;
    if (cfg->dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC && cfg->dec_cfg) {
        const esp_alac_dec_cfg_t *al = cfg->dec_cfg;
        c_cookie_ptr = al->codec_spec_info;
        c_cookie_len = al->spec_info_len;
        memcpy(c_cookie, al->codec_spec_info, al->spec_info_len < 64 ? al->spec_info_len : 64);
    }
    fdec_t *d = malloc(sizeof *d);
    d->type = cfg->dec_type;
    *h = d;
    c_live++;
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_audio_simple_dec_process(esp_audio_simple_dec_handle_t h, esp_audio_simple_dec_raw_t *raw,
                                             esp_audio_simple_dec_out_t *out)
{
    (void)h;
    c_raw_ptr = raw->buffer;
    c_out_ptr = out->buffer;
    const uint32_t n = raw->len / 2;
    if (n * 2 > out->len) { out->needed_size = n * 2; return ESP_AUDIO_ERR_BUFF_NOT_ENOUGH; }
    for (uint32_t i = 0; i < n; i++) out->buffer[2 * i] = out->buffer[2 * i + 1] = raw->buffer[i];
    raw->consumed = n;
    out->decoded_size = n * 2;
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_audio_simple_dec_get_info(esp_audio_simple_dec_handle_t h, esp_audio_simple_dec_info_t *info)
{
    (void)h;
    info->sample_rate = 96000; info->bits_per_sample = 24; info->channel = 2;
    return ESP_AUDIO_ERR_OK;
}
void esp_audio_simple_dec_close(esp_audio_simple_dec_handle_t h) { free(h); c_live--; }
