/* SPDX-License-Identifier: MIT
 *
 * The syscall interface between a U-mode context and whatever hosts it, and
 * the ecall stubs a U-mode program calls them with.
 *
 * Register convention (the RISC-V norm): a7 the number, a0..a6 the
 * arguments, a0 the result. The host writes the result into the saved a0 and
 * steps the saved pc past the ecall, so the program resumes at the next
 * instruction with every other register as it left it.
 *
 * Numbers below FECKLESS_SYS_EXT_BASE are this component's. Numbers from
 * there up go to the window's `ext` hook (feckless_umode_run.h), which is
 * where an embedder's own calls -- gfx, files, sockets -- come in.
 */
#pragma once

#include <stdint.h>

#define FECKLESS_SYS_NOP        0   /* round-trips the trap path                  */
#define FECKLESS_SYS_WRITE      1   /* a0 = buffer, a1 = length -> bytes written  */
#define FECKLESS_SYS_DELAY_MS   2   /* a0 = milliseconds -> 0                     */
#define FECKLESS_SYS_YIELD      3   /* give up the core without blocking -> 0     */
#define FECKLESS_SYS_EXIT       4   /* a0 = status; does not return               */
#define FECKLESS_SYS_GETCORE    5   /* -> the core this runs on (U-mode cannot read mhartid) */
#define FECKLESS_SYS_EXT_BASE   16

/* In a0 when the host rejects the call. A call that can legitimately return
 * either of these must use an out-pointer instead. */
#define FECKLESS_SYS_ERR_BADNR  ((uint32_t)-1)   /* no such call, or the host has no handler */
#define FECKLESS_SYS_ERR_FAULT  ((uint32_t)-2)   /* a pointer outside the window's memory    */

#if defined(__riscv)

/* The U-mode side. Only the number and the arguments cross; everything else
 * in the register file is the program's again on return. */
static inline uint32_t feckless_umode_syscall3(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2)
{
    register uint32_t r0 __asm__("a0") = a0;
    register uint32_t r1 __asm__("a1") = a1;
    register uint32_t r2 __asm__("a2") = a2;
    register uint32_t r7 __asm__("a7") = nr;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

#define feckless_umode_syscall0(nr)          feckless_umode_syscall3((nr), 0, 0, 0)
#define feckless_umode_syscall1(nr, a)       feckless_umode_syscall3((nr), (a), 0, 0)
#define feckless_umode_syscall2(nr, a, b)    feckless_umode_syscall3((nr), (a), (b), 0)

#endif /* __riscv */
