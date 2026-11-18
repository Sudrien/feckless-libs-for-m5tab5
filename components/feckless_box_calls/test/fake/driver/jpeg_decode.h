#pragma once
/* u-feckless-0065: the part of ESP-IDF's JPEG decoder box_image.c uses,
 * the shapes as IDF 6.1's. fake_periph.c records the calls. */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct jpeg_decoder_t *jpeg_decoder_handle_t;
typedef enum { JPEG_DECODE_OUT_FORMAT_RGB888 = 0, JPEG_DECODE_OUT_FORMAT_RGB565 = 1 } jpeg_dec_output_format_t;
typedef enum { JPEG_DEC_ALLOC_INPUT_BUFFER = 0, JPEG_DEC_ALLOC_OUTPUT_BUFFER = 1 } jpeg_dec_buffer_alloc_direction_t;
typedef struct { jpeg_dec_output_format_t output_format; int rgb_order; int conv_std; } jpeg_decode_cfg_t;
typedef struct { int intr_priority; int timeout_ms; struct { uint32_t allow_pd : 1; } flags; } jpeg_decode_engine_cfg_t;
typedef struct { uint32_t width; uint32_t height; int sample_method; } jpeg_decode_picture_info_t;
typedef struct { jpeg_dec_buffer_alloc_direction_t buffer_direction; } jpeg_decode_memory_alloc_cfg_t;
esp_err_t jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *cfg, jpeg_decoder_handle_t *ret);
esp_err_t jpeg_del_decoder_engine(jpeg_decoder_handle_t eng);
esp_err_t jpeg_decoder_get_info(const uint8_t *bs, uint32_t n, jpeg_decode_picture_info_t *info);
esp_err_t jpeg_decoder_process(jpeg_decoder_handle_t eng, const jpeg_decode_cfg_t *cfg, const uint8_t *bs, uint32_t n,
                               uint8_t *out, uint32_t out_size, uint32_t *decoded);
