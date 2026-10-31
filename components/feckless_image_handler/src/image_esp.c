/*
 * image_esp.c -- see feckless_image_esp.h. Defeatist's albumart.c, the
 * parts that touch the chip, behind img_ops_t.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_image_esp.h"

#include <stdlib.h>
#include <string.h>

#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"           /* espressif/esp_jpeg: TJpgDec */

#include "feckless_image_jxl.h"
#include "feckless_image_png.h"
#include "feckless_image_stb.h"

static const char *TAG = "img_esp";

/* ---- heap ----------------------------------------------------------- */
static size_t ps_free(void)    { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static size_t ps_largest(void) { return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }

/* ---- idle priority (5093, 1104) -------------------------------------
 * A 3000x3000 cover took 5.8 s of CPU in one blocking esp_jpeg_decode()
 * on a priority-1 task and the task watchdog fired on IDLE0. Neither
 * TJpgDec nor stb_image can be asked to yield part-way, so the calling
 * task drops to the idle priority for the length of the decode: idle
 * shares its slices and is fed, and everything else preempts it. */
static unsigned long soft_begin(void)
{
    const UBaseType_t prio = uxTaskPriorityGet(NULL);
    if (prio > tskIDLE_PRIORITY) vTaskPrioritySet(NULL, tskIDLE_PRIORITY);
    return (unsigned long)prio;
}

static void soft_end(unsigned long token)
{
    const UBaseType_t prio = (UBaseType_t)token;
    if (uxTaskPriorityGet(NULL) != prio) vTaskPrioritySet(NULL, prio);
}

/* ---- the big-stack runner (6147) ------------------------------------
 * A decoder that needs more stack than the caller has runs on a task of
 * its own whose stack is PSRAM, at idle priority; the caller waits. One
 * at a time (a decode is one at a time), so one TCB serves; it is
 * internal RAM, which a PSRAM stack's TCB must be. The task suspends
 * itself rather than deleting itself because the stack is the caller's
 * to free. */
typedef struct { void (*fn)(void *); void *arg; TaskHandle_t waiter; } job_t;

static DRAM_ATTR StaticTask_t s_tcb;

static void bigstack_task(void *p)
{
    job_t *j = p;
    j->fn(j->arg);
    xTaskNotifyGive(j->waiter);
    vTaskSuspend(NULL);
}

static bool run_on_big_stack(const char *name, size_t stack_bytes, void (*fn)(void *), void *arg)
{
    StackType_t *stack = heap_caps_malloc(stack_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!stack) return false;
    job_t job = { .fn = fn, .arg = arg, .waiter = xTaskGetCurrentTaskHandle() };
    TaskHandle_t t = xTaskCreateStatic(bigstack_task, name, stack_bytes, &job,
                                       tskIDLE_PRIORITY, stack, &s_tcb);
    if (!t) { heap_caps_free(stack); return false; }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (eTaskGetState(t) != eSuspended) vTaskDelay(1);
    vTaskDelete(t);
    heap_caps_free(stack);
    return true;
}

/* ---- the decoders' answers, in one place ----------------------------- */
static img_err_t from_stb(img_stb_err_t e)
{
    switch (e) {
    case IMG_STB_OK:     return IMG_OK;
    case IMG_STB_NO_MEM: return IMG_ERR_NO_MEM;
    case IMG_STB_TOO_BIG:
    case IMG_STB_NOT_JPEG: return IMG_ERR_UNSUPPORTED;
    default:             return IMG_ERR_FAILED;
    }
}
static img_err_t from_png(img_png_err_t e)
{
    switch (e) {
    case IMG_PNG_OK:     return IMG_OK;
    case IMG_PNG_NO_MEM: return IMG_ERR_NO_MEM;
    case IMG_PNG_TOO_BIG:
    case IMG_PNG_NOT_PNG: return IMG_ERR_UNSUPPORTED;
    default:             return IMG_ERR_FAILED;
    }
}
static img_err_t from_jxl(img_jxl_err_t e)
{
    switch (e) {
    case IMG_JXL_OK:     return IMG_OK;
    case IMG_JXL_NO_MEM: return IMG_ERR_NO_MEM;
    case IMG_JXL_TOO_BIG:
    case IMG_JXL_NOT_JXL:
    case IMG_JXL_UNSUPPORTED: return IMG_ERR_UNSUPPORTED;
    default:             return IMG_ERR_FAILED;
    }
}

/* ---- stb_image, GIF on its own stack, PNG, J40 ----------------------- */
typedef struct {
    const uint8_t *in; size_t len; int box_w, box_h; uint16_t bg;
    uint16_t *px; size_t size; int w, h; const char *why;
    int kind;                               /* 0 stb/gif, 1 png, 2 jxl */
    img_err_t err;
} sw_job_t;

static void sw_job(void *arg)
{
    sw_job_t *j = arg;
    switch (j->kind) {
    case 1:  j->err = from_png(img_png_decode_rgb565(j->in, j->len, j->box_w, j->box_h, j->bg,
                                   &j->px, &j->size, &j->w, &j->h, &j->why)); break;
    case 2:  j->err = from_jxl(img_jxl_decode_rgb565(j->in, j->len, j->box_w, j->box_h, j->bg,
                                   &j->px, &j->size, &j->w, &j->h, &j->why)); break;
    default: j->err = from_stb(img_stb_decode_rgb565(j->in, j->len, j->box_w, j->box_h,
                                   &j->px, &j->size, &j->w, &j->h, &j->why)); break;
    }
}

static img_err_t sw_run(int kind, size_t stack, const char *name, const uint8_t *in, size_t len,
                        int box_w, int box_h, uint16_t bg, img_pic_t *pic, const char *dec)
{
    sw_job_t j = { .in = in, .len = len, .box_w = box_w, .box_h = box_h, .bg = bg,
                   .kind = kind, .err = IMG_ERR_FAILED };
    if (stack) {
        if (!run_on_big_stack(name, stack, sw_job, &j)) {
            pic->why = "no PSRAM for the decoder's stack";
            return IMG_ERR_NO_MEM;
        }
    } else {
        sw_job(&j);
    }
    pic->why = j.why;
    if (j.err != IMG_OK) return j.err;
    pic->px = j.px; pic->size = j.size; pic->w = j.w; pic->h = j.h; pic->stride = j.w;
    pic->decoder = dec;
    return IMG_OK;
}

static img_err_t d_stb(const uint8_t *in, size_t len, int bw, int bh, img_pic_t *p)
{ return sw_run(0, 0, "", in, len, bw, bh, 0, p, "stb"); }
static img_err_t d_gif(const uint8_t *in, size_t len, int bw, int bh, img_pic_t *p)
{ return sw_run(0, IMG_GIF_STACK, "img_gif", in, len, bw, bh, 0, p, "stb"); }
static img_err_t d_png(const uint8_t *in, size_t len, int bw, int bh, uint16_t bg, img_pic_t *p)
{ return sw_run(1, 0, "", in, len, bw, bh, bg, p, "pngle"); }
static img_err_t d_jxl(const uint8_t *in, size_t len, int bw, int bh, uint16_t bg, img_pic_t *p)
{ return sw_run(2, IMG_JXL_STACK, "img_jxl", in, len, bw, bh, bg, p, "j40"); }

/* ---- TJpgDec ---------------------------------------------------------
 * The work buffer is OURS (1104): esp_jpeg's own pool, for 4:2:0 with
 * CONFIG_JD_FASTDECODE=1, is bigger than internal RAM can spare and
 * jd_prepare() failed with JDR_MEM1 before a single MCU. 8 KB of
 * internal DRAM is enough for 4:2:0 either way. TJpgDec writes RGB565
 * tightly packed, so the stride is the width it returns. */
#define JPEG_SW_WORKBUF  (8u * 1024)

static img_err_t d_tjpgd(const uint8_t *in, size_t len, uint32_t iw, uint32_t ih,
                         int box_w, int box_h, img_pic_t *pic)
{
    static const esp_jpeg_image_scale_t scales[] = {
        JPEG_IMAGE_SCALE_0, JPEG_IMAGE_SCALE_1_2, JPEG_IMAGE_SCALE_1_4, JPEG_IMAGE_SCALE_1_8,
    };
    int idx = img_tjpgd_first_scale(iw, ih, box_w, box_h);

    uint8_t *work = heap_caps_malloc(JPEG_SW_WORKBUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!work) { pic->why = "no DRAM for the JPEG work buffer"; return IMG_ERR_NO_MEM; }

    for (; idx <= 3; idx++) {
        const int w = (int)(iw >> idx), h = (int)(ih >> idx);
        if (w < 1 || h < 1) break;
        const size_t need = (size_t)w * h * 2;
        if (ps_largest() < need) continue;
        uint8_t *buf = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) continue;

        esp_jpeg_image_cfg_t cfg = {
            .indata = (uint8_t *)in, .indata_size = len,
            .outbuf = buf, .outbuf_size = need,
            .out_format = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale = scales[idx],
            /* NOT swapped: the shadow buffer, the panel and the other
             * decoders are all native little-endian RGB565. */
            .flags = { .swap_color_bytes = 0 },
            .advanced = { .working_buffer = work, .working_buffer_size = JPEG_SW_WORKBUF },
        };
        esp_jpeg_image_output_t out = { 0 };
        const esp_err_t err = esp_jpeg_decode(&cfg, &out);
        if (err != ESP_OK) {
            /* esp_jpeg collapses JDR_* into ESP_FAIL; its own E line has
             * the code: 3 JDR_MEM1 (raise JPEG_SW_WORKBUF), 5 JDR_PAR
             * (CONFIG_JD_USE_SCALE off), 6-8 a format TJpgDec cannot read. */
            ESP_LOGW(TAG, "TJpgDec at 1/%d failed (%s): %dx%d out, %u KB workbuf",
                     1 << idx, esp_err_to_name(err), w, h, (unsigned)(JPEG_SW_WORKBUF / 1024));
            heap_caps_free(buf);
            heap_caps_free(work);
            pic->why = "TJpgDec refused the data";
            return IMG_ERR_FAILED;
        }
        heap_caps_free(work);
        pic->px = (uint16_t *)buf; pic->size = need;
        pic->w = (int)out.width; pic->h = (int)out.height; pic->stride = pic->w;
        pic->decoder = "tjpgd";
        ESP_LOGI(TAG, "decoded by TJpgDec at 1/%d: %dx%d", 1 << idx, pic->w, pic->h);
        return IMG_OK;
    }
    heap_caps_free(work);
    pic->why = "no scale of this picture fits";
    return IMG_ERR_NO_MEM;
}

/* ---- the hardware engine --------------------------------------------- */
static jpeg_decoder_handle_t s_engine;

void img_esp_init(void)
{
    if (s_engine) return;
    const jpeg_decode_engine_cfg_t cfg = { .timeout_ms = 5000 };
    const esp_err_t err = jpeg_new_decoder_engine(&cfg, &s_engine);
    if (err != ESP_OK) {
        s_engine = NULL;
        ESP_LOGW(TAG, "no hardware JPEG engine (%s); pictures decode in software", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "hardware JPEG engine ready, kept for the session");
}

static int d_hw_ready(void) { return s_engine != NULL; }

static img_err_t d_hw_info(const uint8_t *in, size_t len, img_hw_info_t *info)
{
    jpeg_decode_picture_info_t pi;
    if (jpeg_decoder_get_info(in, len, &pi) != ESP_OK) return IMG_ERR_FAILED;
    info->width = pi.width;
    info->height = pi.height;
    switch (pi.sample_method) {
    case JPEG_DOWN_SAMPLING_YUV420: info->mcu_w = 16; info->mcu_h = 16; break;
    case JPEG_DOWN_SAMPLING_YUV422: info->mcu_w = 16; info->mcu_h = 8;  break;
    case JPEG_DOWN_SAMPLING_YUV444:
    case JPEG_DOWN_SAMPLING_GRAY:   info->mcu_w = 8;  info->mcu_h = 8;  break;
    default:                        info->mcu_w = 16; info->mcu_h = 16; break;  /* largest, so safe */
    }
    return IMG_OK;
}

static img_err_t d_hw_decode(const uint8_t *in, size_t len, const img_hw_info_t *info,
                             uint32_t pad_w, uint32_t pad_h, img_pic_t *pic)
{
    /* The engine DMAs straight out of the input buffer: the bitstream has
     * to live in memory it can reach, padded to the alignment it asks
     * for. A plain malloc'd copy is not enough. */
    const jpeg_decode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    size_t in_size = 0, out_size = 0;
    uint8_t *dma_in = jpeg_alloc_decoder_mem(len, &in_cfg, &in_size);
    if (!dma_in) { pic->why = "no DMA input buffer"; return IMG_ERR_NO_MEM; }
    memcpy(dma_in, in, len);

    const jpeg_decode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    const size_t want = (size_t)pad_w * pad_h * 2;
    uint8_t *rgb = jpeg_alloc_decoder_mem(want, &out_cfg, &out_size);
    if (!rgb) { free(dma_in); pic->why = "no DMA output buffer"; return IMG_ERR_NO_MEM; }

    /* BGR is the setting that gives RGB565 as the rest of the codebase
     * has it: rgb_order selects a DMA2D BYTE scramble after the colour
     * conversion, not a colour order, and ORDER_RGB swaps red and blue in
     * a little-endian 16-bit word (a gold cover came out silver-blue).
     * conv_std is stated: BT.601 is right for JPEG. */
    const jpeg_decode_cfg_t cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t decoded = 0;
    const esp_err_t hw = jpeg_decoder_process(s_engine, &cfg, dma_in, len, rgb, out_size, &decoded);
    free(dma_in);
    if (hw != ESP_OK) {
        /* 5055: refuses some JPEGs outright ("sizes not divisible by 8"). */
        ESP_LOGW(TAG, "hardware decode refused (%s)", esp_err_to_name(hw));
        free(rgb);
        pic->why = "the hardware engine refused the picture";
        return IMG_ERR_FAILED;
    }
    if (decoded != (uint32_t)want) {
        ESP_LOGW(TAG, "decoder wrote %u bytes, expected %u for %ux%u",
                 (unsigned)decoded, (unsigned)want, (unsigned)pad_w, (unsigned)pad_h);
        free(rgb);
        pic->why = "the hardware wrote a different size than its header implied";
        return IMG_ERR_FAILED;
    }
    pic->px = (uint16_t *)rgb;
    pic->size = want;
    pic->w = (int)info->width;
    pic->h = (int)info->height;
    pic->stride = (int)pad_w;
    pic->decoder = "hardware";
    return IMG_OK;
}

/* ---------------------------------------------------------------------- */
static const img_ops_t s_ops = {
    .psram_free = ps_free, .psram_largest = ps_largest,
    .hw_ready = d_hw_ready, .hw_info = d_hw_info, .hw_decode = d_hw_decode,
    .soft_begin = soft_begin, .soft_end = soft_end,
    .tjpgd = d_tjpgd, .stb = d_stb, .gif = d_gif, .png = d_png, .jxl = d_jxl,
};

const img_ops_t *img_esp_ops(void) { return &s_ops; }
