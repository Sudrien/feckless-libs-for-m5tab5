#pragma once
#include <stdint.h>
typedef struct polyrsp polyrsp_t;
polyrsp_t *polyrsp_open(uint32_t in_rate, uint32_t out_rate, uint32_t max_in_frames);
void polyrsp_close(polyrsp_t *r);
void polyrsp_reset(polyrsp_t *r);
uint32_t polyrsp_max_out(const polyrsp_t *r);
uint32_t polyrsp_process(polyrsp_t *r, const int16_t *in, uint32_t in_frames, int16_t *out);
