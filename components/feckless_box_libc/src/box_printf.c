/* SPDX-License-Identifier: MIT
 *
 * snprintf and its family, for a boxed app: eyalroz/printf v6.4.0
 * (vendor/printf, MIT) under the standard names. printf and vprintf write
 * through putchar_, which hands each character to the host's putchar -- one
 * ecall a character, so a boxed app that prints much should snprintf and puts.
 */
#define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD 1
#include "../vendor/printf/printf.c"

extern int putchar(int c);

void putchar_(char c)
{
    putchar((unsigned char)c);
}
