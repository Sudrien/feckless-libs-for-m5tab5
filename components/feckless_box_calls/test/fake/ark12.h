#pragma once
#include <stdint.h>
/* Small stand-ins for Ark12's tables: only their names and sizes matter. */
#define ARK12_TILES      4
#define ARK12_LIT_BYTES  10
typedef struct { uint16_t first, count, g; } ark12_run_t;
extern const ark12_run_t ark12_runs[2];
extern const uint32_t    ark12_flags[3];
extern const uint16_t    ark12_ckpt[5];
extern const uint32_t    ark12_litbase[6];
extern const uint8_t     ark12_lit[];
extern const uint8_t     ark12_tiles[];
