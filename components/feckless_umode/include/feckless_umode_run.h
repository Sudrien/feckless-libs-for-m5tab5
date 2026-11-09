/* SPDX-License-Identifier: MIT
 *
 * Hosting a U-mode context: the loop that enters it, tells why it came
 * back, and services its syscalls. No ESP-IDF in it -- everything that
 * needs the RTOS comes in through feckless_umode_ops_t -- so the host test
 * runs this file as it is, with a scripted stand-in for the context.
 *
 * Run from an ordinary task, in M-mode; blocking in an op is legal, because
 * the trap vector does no dispatching (feckless_umode.h).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "feckless_umode.h"
#include "feckless_umode_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct feckless_umode_window feckless_umode_window_t;

/* An embedder's syscall (number >= FECKLESS_SYS_EXT_BASE). args are a0..a6
 * as the program left them. Set *ret and return true to resume the program;
 * return false to retire it (end reason FECKLESS_UMODE_END_KILLED). Pointers
 * from the program must go through feckless_umode_range_ok() first. */
typedef bool (*feckless_umode_ext_fn)(feckless_umode_window_t *w, uint32_t nr,
                                      const uint32_t args[7], uint32_t *ret);

/* One U-mode context and what the host knows about it.
 *
 * THE WINDOW MUST NOT BE IN MEMORY THE PROGRAM CAN WRITE. ctx holds its
 * saved registers and pc; a program that can reach them can rewrite its own
 * return address, or this host's. feckless_umode_run() refuses a window that
 * overlaps [lo, hi), and the rest -- not putting it under any other grant --
 * is the caller's. */
struct feckless_umode_window {
    feckless_umode_ctx_t ctx;
    uintptr_t lo;                   /* [lo, hi): the memory the program may hand the host pointers into */
    uintptr_t hi;
    feckless_umode_ext_fn ext;      /* NULL: numbers >= EXT_BASE are BADNR */
    void *ext_arg;
    /* counters, written by feckless_umode_run() */
    uint32_t syscalls;              /* ecalls serviced, the exit included  */
    uint32_t irqs;                  /* interrupts taken inside the window  */
    uint32_t migrations;            /* times the host task moved core between windows */
    int core;
};

/* What the host needs from the RTOS and the console. A NULL op makes its
 * syscall answer FECKLESS_SYS_ERR_BADNR. enter is required. */
typedef struct {
    uint32_t (*enter)(feckless_umode_ctx_t *ctx);      /* feckless_umode_enter */
    void (*delay_ms)(uint32_t ms);
    void (*yield)(void);
    uint32_t (*core_id)(void);
    void (*write)(const char *buf, uint32_t len);      /* buf is read in place; already range-checked */
    /* u-feckless-0028: the FPU, which the RTOS enables per task, lazily: a
     * task's first float instruction traps, in M-mode, and the RTOS turns
     * the FPU on for it (and saves its state from then on). In U-mode that
     * trap is the program's illegal instruction instead. fpu_on, if set, is
     * called before every window, in the host task, to take that trap there;
     * fpu_off, if set, says the FPU is off now -- after an illegal
     * instruction, a sign it was switched off between fpu_on and the
     * window, so the window is retried (a few times) rather than ended. */
    void (*fpu_on)(void);
    bool (*fpu_off)(void);
} feckless_umode_ops_t;

typedef enum {
    FECKLESS_UMODE_END_EXIT,        /* SYS_EXIT; status is the program's */
    FECKLESS_UMODE_END_FAULT,       /* a trap that is neither syscall nor interrupt: mcause, mtval, pc */
    FECKLESS_UMODE_END_STACK,       /* sp left [u_sp_min, u_sp_max]; sp_pc is where the guard latched it */
    FECKLESS_UMODE_END_KILLED,      /* an ext syscall asked for it */
    FECKLESS_UMODE_END_REFUSED,     /* bad arguments, or the window overlaps its own memory */
} feckless_umode_end_reason_t;

typedef struct {
    feckless_umode_end_reason_t reason;
    uint32_t status;                /* EXIT */
    uint32_t mcause;                /* FAULT */
    uint32_t mtval;                 /* FAULT: the offending instruction or address */
    uint32_t pc;                    /* FAULT, STACK, EXIT, KILLED: the program's pc at the trap */
    uint32_t sp_pc;                 /* STACK */
} feckless_umode_end_t;

/* Enter w->ctx until it ends. Per window, in this order: a stack-guard
 * verdict (it is independent of the trap that brought control back, so it
 * is read first), an interrupt (re-enter), an ecall (serviced, pc stepped,
 * a0 set), anything else (a fault; the window is not resumed). */
feckless_umode_end_t feckless_umode_run(feckless_umode_window_t *w, const feckless_umode_ops_t *ops);

/* May the host read or write [addr, addr+len) on the program's behalf? Only
 * inside [w->lo, w->hi), and with no wrap. The PMP keeps the PROGRAM out of
 * the kernel; this keeps the HOST from being talked into crossing the line
 * for it -- and, while grants share a range, from one program to another. */
bool feckless_umode_range_ok(const feckless_umode_window_t *w, uintptr_t addr, uint32_t len);

/* Length of the NUL-terminated string at addr, looking at no byte outside
 * the window and none past max; -1 if it is not terminated within both. */
int feckless_umode_strlen(const feckless_umode_window_t *w, uintptr_t addr, uint32_t max);

/* A name for an exception code (mcause & 0xfff), for the log. */
const char *feckless_umode_cause_name(uint32_t mcause);

#ifdef __cplusplus
}
#endif
