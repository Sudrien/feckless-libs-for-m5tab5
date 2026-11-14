/* newlib's <machine/ieeefp.h> as it reads for the ESP32-P4: little-endian,
 * no double FPU, so the old fdlibm code (__OBSOLETE_MATH). */
#pragma once
#define __IEEE_LITTLE_ENDIAN
#define __OBSOLETE_MATH_DEFAULT 1
#define __OBSOLETE_MATH 1
