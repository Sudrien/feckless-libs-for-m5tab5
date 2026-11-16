/* SPDX-License-Identifier: MIT */
/*
 * Host stand-in for ESP-IDF 6.1's driver/ppa.h (u-feckless-0057): the
 * names gfx.c uses, with the fields and anonymous unions laid out as
 * IDF's, so gfx.c's initialisers compile as they would there. The SRM
 * itself is blittest.c's model of it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "shim.h"

typedef enum { PPA_OPERATION_SRM, PPA_OPERATION_BLEND, PPA_OPERATION_FILL, PPA_OPERATION_INVALID } ppa_operation_t;
typedef enum {
    PPA_SRM_ROTATION_ANGLE_0,       /* hal/ppa_types.h: counter-clockwise */
    PPA_SRM_ROTATION_ANGLE_90,
    PPA_SRM_ROTATION_ANGLE_180,
    PPA_SRM_ROTATION_ANGLE_270,
} ppa_srm_rotation_angle_t;
typedef enum { PPA_SRM_COLOR_MODE_RGB565 = 0x10, PPA_SRM_COLOR_MODE_RGB888 = 0x18 } ppa_srm_color_mode_t;
typedef enum { PPA_DATA_BURST_LENGTH_128 = 128 } ppa_data_burst_length_t;
typedef enum { PPA_TRANS_MODE_BLOCKING, PPA_TRANS_MODE_NON_BLOCKING } ppa_trans_mode_t;
typedef enum { PPA_ALPHA_NO_CHANGE } ppa_alpha_update_mode_t;

typedef struct ppa_client_t *ppa_client_handle_t;
typedef struct {
    ppa_operation_t oper_type;
    uint32_t max_pending_trans_num;
    ppa_data_burst_length_t data_burst_length;
    struct { uint32_t allow_pd: 1; } flags;
} ppa_client_config_t;

typedef struct {
    const void *buffer;
    uint32_t pic_w, pic_h, block_w, block_h, block_offset_x, block_offset_y;
    union { ppa_srm_color_mode_t srm_cm; uint32_t cm; };
} ppa_in_pic_blk_config_t;
typedef struct {
    void *buffer;
    uint32_t buffer_size;
    uint32_t pic_w, pic_h, block_offset_x, block_offset_y;
    union { ppa_srm_color_mode_t srm_cm; uint32_t cm; };
} ppa_out_pic_blk_config_t;
typedef struct {
    ppa_in_pic_blk_config_t in;
    ppa_out_pic_blk_config_t out;
    ppa_srm_rotation_angle_t rotation_angle;
    float scale_x, scale_y;
    bool mirror_x, mirror_y;
    bool rgb_swap, byte_swap;
    ppa_alpha_update_mode_t alpha_update_mode;
    union { uint32_t alpha_fix_val; float alpha_scale_ratio; };
    ppa_trans_mode_t mode;
    void *user_data;
} ppa_srm_oper_config_t;

esp_err_t ppa_register_client(const ppa_client_config_t *config, ppa_client_handle_t *ret_client);
esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t ppa_client, const ppa_srm_oper_config_t *config);
