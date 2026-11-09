/* SPDX-License-Identifier: MIT
 * src/box_fpconv.c against the host's own conversions, bit for bit. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

double box___extendsfdf2(float);
float box___floatdisf(int64_t), box___floatundisf(uint64_t);
int64_t box___fixsfdi(float);
uint64_t box___fixunssfdi(float);
int box___issignalingf(float);

static int failures;
static uint64_t s = 88172645463325252ull;
static uint64_t r64(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static float fbits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t ubits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    long n = 0;
    /* every float whose top 16 bits vary, low bits random: all exponents, both signs */
    for (uint32_t hi = 0; hi < 0x10000; hi++) {
        for (int k = 0; k < 16; k++) {
            const float f = fbits(hi << 16 | (uint32_t)(r64() & 0xffff));
            n++;
            const double d = box___extendsfdf2(f), want = (double)f;
            if (isnan(f) ? !isnan(d) : memcmp(&d, &want, 8)) {
                if (failures++ < 5) printf("FAIL extend %a\n", f);
            }
            if (isnan(f)) continue;
            /* fcvt's saturation; C leaves out-of-range undefined, so compare in range */
            if (f > -0x1p63f && f < 0x1p63f && box___fixsfdi(f) != (int64_t)f) {
                if (failures++ < 5) printf("FAIL fixsfdi %a\n", f);
            }
            if (f >= 0 && f < 0x1p64f && box___fixunssfdi(f) != (uint64_t)f) {
                if (failures++ < 5) printf("FAIL fixunssfdi %a\n", f);
            }
        }
    }
    if (box___fixsfdi(0x1p70f) != INT64_MAX || box___fixsfdi(-0x1p70f) != INT64_MIN ||
        box___fixsfdi(NAN) != INT64_MAX || box___fixunssfdi(-1.0f) != 0 ||
        box___fixunssfdi(0x1p70f) != UINT64_MAX || box___fixsfdi(-0x1p63f) != INT64_MIN) {
        printf("FAIL saturation\n"); failures++;
    }
    for (int i = 0; i < 4000000; i++) {
        uint64_t u = r64() >> (r64() & 63);
        n += 2;
        const float a = box___floatundisf(u), b = (float)u;
        if (ubits(a) != ubits(b)) { if (failures++ < 5) printf("FAIL floatundisf %llu\n", (unsigned long long)u); }
        const int64_t v = (int64_t)u * ((r64() & 1) ? -1 : 1);
        const float c = box___floatdisf(v), d = (float)v;
        if (ubits(c) != ubits(d)) { if (failures++ < 5) printf("FAIL floatdisf %lld\n", (long long)v); }
    }
    const int64_t edge[] = { 0, 1, -1, INT64_MAX, INT64_MIN, (1ll << 24) + 1, (1ll << 25) + 3,
                             0x7fffff8000000000ll, 0x7fffffc000000001ll };
    for (unsigned i = 0; i < sizeof edge / sizeof *edge; i++)
        if (ubits(box___floatdisf(edge[i])) != ubits((float)edge[i])) { printf("FAIL floatdisf edge %u\n", i); failures++; }
    if (box___floatundisf(UINT64_MAX) != (float)UINT64_MAX) { printf("FAIL u max\n"); failures++; }
    if (!box___issignalingf(fbits(0x7f800001u)) || box___issignalingf(NAN) || box___issignalingf(1.0f)) {
        printf("FAIL issignalingf\n"); failures++;
    }
    if (failures) printf("fpconv: %d FAILED\n", failures);
    else printf("fpconv: all passed (%ld conversions)\n", n);
    return failures != 0;
}
